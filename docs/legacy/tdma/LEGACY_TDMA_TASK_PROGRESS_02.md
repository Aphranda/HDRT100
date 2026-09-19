# TDMA 基础件历史任务进度 02

Status: Frozen
Domain: TDMA
Canonical: `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_02.md`
Related: `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-09-19

> 本文件为历史证据归档，保留当时状态、失败及未编号旧 checkpoint；当前任务以主域 TODO 为准。

### TDMA-PROGRESS-20260912-029 - origin 与近端从站 DATA 相位组合归因

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-origin-phase-ab/`。
- 基线 `dcb5eb09d5ca1379dd63bfc1744e9bd59a7a2d35`；四板保持 build
  `20260912144225`，源码指纹
  `b0d8daf294f26844c8cb01da84ff942101de8f39940bb30e88aa5a24caf345f0`。
  本轮仅执行已授权的有界诊断与文档更新，没有固件/PIO/构建/正式工具实现变更、
  重新 OTA 或新 P3 凭证。前轮真实 P3 的严格失败仍保留，不将本轮对照提升为 P3 通过。
- 最初 SCK 行对照在 `load_config()` 预检时因不能在 opposite edge 前 re-arm 被拒绝，
  尚未发送硬件命令；失败保留在 `phase-ab-command-r1.log/json`，未放宽准入。
  随后的 NO3 DATA 对照使用当前矩阵行 `1→0→1`，实际 phase `15→14→15`，
  三段实际 soak `24.906/25.141/24.485 s`，四板 transport bad 增量均零。
- 为复现前轮差异，使用封存旧矩阵 `tdma-flight-owner-attribution/matrix-strict-r1.json`
  的合法行，矩阵原件与 `passed/diagnostic_continue` 未编辑；固件源与前轮相同，
  但矩阵生成早于当前 build，因此只作 diagnostic replay。MARK/SCK 及其他从站
  DATA 参数不变，以下均按各板原始 physical 读回确认，不能按矩阵 link 编号猜测。

  | 窗口 | 旧矩阵行 | origin / NO2 DATA phase | 实际 soak（s） | NO1 RX good / bad 增量 | 四板掉线事件 |
  |---|---|---|---|---|---|
  | `no2-a1` | 7 | 15 / 15 | 25.657 | 6188 / 0 | 均零 |
  | `no2-b` | 5 | 15 / 14 | 25.984 | 6170 / 18 | NO1 一次，随后恢复 |
  | `no2-a-recovery` | 7 | 15 / 15 | 25.859 | 6278 / 0 | 均零 |
  | `origin14-no2-14` | 1 | 14 / 14 | 25.438 | 6059 / 0 | 均零 |
  | `origin14-no2-15` | 3 | 14 / 15 | 25.656 | 6194 / 0 | 均零 |

- 所有窗口的 NO2/NO3/NO4 CRC 增量均零，无 physical fault 增长或 counter regression。
  原 NO2 A/B/A 脚本在 B 段发现 down event 后终止，`no2-a2` 未执行；
  `no2-a-recovery` 是核实 STOP 并恢复当前矩阵后的独立恢复对照，不能改写原脚本为完成。
  两个 origin 组合均完成有限 soak；第二段后的独立 STOP helper 在 NO3 收到
  `<timeout>`，使组合包装器失败。后续原始状态已是四板 stopped，但该命令失败保留，
  不用状态读回覆盖。最终 `restore-current-r3/` 经现有 owner 生命周期恢复当前矩阵
  active row 并再次确认 STOP；实际 soak `8.172 s`，四板 RX good 增量
  `1990/1998/1997/1998`，CRC 与 down 增量均零。
- `phase-analysis-r1.json` 逐样本校验 build、相位读回、matrix generation，并从首末
  runtime counter 与逐区间增量交叉核算接收统计。B 段保存的 schedule/profile
  异常 bit 均为 `1→0`，其中可在同字段确定的下一线上 bit 为 `0`；跨字段下一 bit
  未推断。这与前轮偏晚采样线索一致，但尚未保留同一坏帧的物理波形与接收私有副本。
  runtime 与 CRC diagnostic 的坏帧 sequence 在多个同 sample 查询中不同，分析
  显式记录并分开处理，不能拼成同一错误帧。
- 结论：在受测条件中，相位组合可触发和消除有限窗口内的错误，排查重点收敛到
  NO2→origin 返回 DATA 的采样余量；不能据此直接冻结新 phase。origin DATA 参数
  同时影响 TX 与 RX，NO2 参数同时影响采样与输出重定时，仍需区分具体机制。
  所有运行均保留 startup timeout，`passed/closed_loop_passed/realtime_gate_passed=false`，
  `diagnostic_continue=true`；有限 soak 通过不能替代完整 phase/WCET 或稳定性验收。
- 文档自回归、主控原始证据复核、独立文档提交与 SHA 封存见本根的检查日志、
  `review-r1.json`、`slice-manifest.json`、`commit-proof.json`。本轮为归因进展，
  `TDMA-FLIGHT-002F` 仍 IN PROGRESS，切片 PARTIAL；无 registry/C11 状态变更。
- 下一 gate：绑定实际 PIO 时序、返回 DATA 有效窗口和同一坏帧副本，建立可复核的
  采样余量及反例，再由 Calibration owner 提出修复并完成当前源码软件/构建/P3。
  正确性闭合后继续 overlay/RX 削减；保留 `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`、
  正式 RAM、特等席逐圈保全和动态节点后续项的原有门禁。

### TDMA-PROGRESS-20260912-028 - 启动采样截止检查与 origin CRC 相位调查

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-origin-crc/`。
- 基线：`a1ae54f17dbd6d28625e97fe2d82a5aee99e4245`；此前封存的
  `tdma-flight-owner-attribution/` 原件只读引用。当前切片实现仅修改启动验收工具与
  回归测试，未改变 firmware/PIO、跨核 owner、FIFO、wire 或现有完整 phase 门禁。
- 确认并修复 `wait_startup_barrier()` 的迟到误判：poll 后再次检查剩余期限，整组
  串口查询完成后复验；健康性与按期完成分别留证，迟到样本不能增加稳定计数。
  正常与 diagnostic-continue 路径均保留失败，未增加 startup timeout 或减少稳定样本数。
  这里验收的是主机完成观察的期限，不是直接测量板端启动延迟。
- `startup-negative-r0.log` 在旧实现上复现失败：按前轮样本完成时刻
  `2.063/3.969/5.891/7.735 s` 重放，首个区间有 pipeline-fill 错误，随后三个健康
  区间的末次完成超过配置的 `6 s`，旧工具仍通过。新实现拒绝该路径，并覆盖按时完成、
  恰在截止点完成、poll 耗尽期限后不再查询；相关工具/P3 回归 `156 passed`，见
  `startup-regression-r1.log`。
- 原件重放：`sampling-replay-r1.json` 从前轮普通与自主 NO1 SD 二进制原件重建，
  校验 SHA 与 capture JSON 一致，按固件实际 `RX_CLK/RX_CS/TX_DATA` 引脚组合解码。
  `sampling-bits-r1.json` 固定同一组 bit/clock 位置，保留 CRC-invalid/PHY-invalid
  结果，避免自动重对齐掩盖坏采样。所选两帧在相对时钟 `16–56 ns` 样点解码正常，
  `64 ns` 样点不正常；这是有限、量化后的相位线索，不能证明实际固件坏帧的损坏位置，
  更不能由离线选择直接冻结新相位。普通 origin 原有 CRC 失败仍保留。
- `bad-header-correlations-r2.json` 从 CRC 有效原始帧读取 transport ring-profile
  CRC，确认保存的三次 profile 字段异常均为单 bit `1→0` 且下一线上 bit 为 `0`；
  对应有限波形下降沿在相对时钟 `64/72 ns` 的量化样点，符合但未证明偏晚采样假设。
  首版 `bad-header-correlations-r1.json` 误将 operating-profile CRC 与 transport
  ring-profile CRC 比较，r2 明确标为无效分析并保留，不用其差异或结论作为证据。
- 已核对普通 TX 私有 DMA 缓冲与完成握手、RX ring 完成范围与覆盖复验、异步解析
  claim/release 生命周期；未形成内存损坏或提前复用的可复现根因。首个 header 差异
  `hop_count` 可合法变化，不能拿该位置充当坏字节定位。
- 当前 Release A/B/Boot 和 Flash link gates 通过；build `20260912144225`，源码指纹
  `b0d8daf294f26844c8cb01da84ff942101de8f39940bb30e88aa5a24caf345f0`，
  `1018` 个源码文件、`634` 个 app 编译单元均为容量 `6`。两应用链接 RAM 增量均为
  `0 B`，剩余仍 `5168 B`，正式 RAM 缺口未改善，见 `source-checkpoint-r1.json`。
- 当前源码真实 P3：r1 完成四板 OTA，但 P0T 未检测到 NO4→NO1 回传，未生成凭证；
  `p3-r1-state.json` 确认四板停止。r2 同包复位复测恢复拓扑并完成 quick diagnostic
  flow，凭证 `passed=true/strict_gates_passed=false`；保留 NO2 ARM result `8` 和
  running handoff 失败。独立 `matrix-strict-r1.json` 从本轮原始校准结果生成，
  `passed=true/diagnostic_continue=false`。该恢复链不等于一次连续严格通过。
- 单独启动复核 `startup-hardware-r1/` 使用同 build/新矩阵、`6 s` startup timeout
  与 `5 ms` 串口 read quantum：样本完成 `2.188/4.375/6.641 s`，首段 pipeline fill
  失败，末段健康但迟到，已正确拒绝。错误 JSON 保留在严格路径的 exception/summary 中；
  没有进入 soak 或交出 running loop，最终四板 STOP 确认通过。
- 诊断复核 `startup-hardware-r2/` 的样本完成 `1.922/3.812/5.906 s`：首段 fill
  失败，随后仅两个稳定区间，poll 耗尽剩余期限后未再启动查询。工具按授权继续有限
  soak，但 startup/closed-loop/realtime 仍为失败，`diagnostic_continue=true`。
  soak 主机窗口实际 `8.640 s`（配置目标 `1 s`，查询自身耗时另计），四板 RX 增量
  `2126/2129/2134/2122`，transport bad 增量均零、无 physical fault 或 down event；
  最终四板 STOP 成功。新矩阵 NO2 的 DATA phase 相比前轮改变，其他条件也未完全控制，
  该有限零错误窗口不能证明旧 CRC 问题已修复，也不替代完整 phase/WCET 验收。
- 主控复核、源码/文档分离提交及 SHA 封存分别见 `review-r1.json`、
  `slice-manifest.json`、`commit-proof.json`。本次完成的是启动截止检查修复和有限
  CRC 调查，切片结论 PARTIAL；未提升产品、registry 或 C11 状态。
- 状态与下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS。普通 CRC 尚未恢复正确性，
  完整 WCET、正式 RAM 与特等席逐圈保全未验收；先关联同一坏帧的物理采样与观察副本，
  验证采样边缘假设，再推进 overlay/RX 开销削减。registry/C11、预算与动态节点顺序不变。

### TDMA-PROGRESS-20260912-027 - owner 剩余开销归因与普通 CRC 失败

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-owner-attribution/`。
- 实现：`TDMA_SERVICE_TIMING_VERSION` 升至 V4，在原顺序后追加 runtime、runtime
  publish、intent dispatch、adapter prologue、RX handoff 与 adapter status 子项。
  普通 RX 工位区间包含执行到的 capture/parse，自主 origin 另有专用分支；零值不
  表示父路径无成本。所有阶段保留在同条完整记录，跨记录峰值与包含式区间不得求和。
  无生命周期、FIFO、wire、PIO/DMA 分区或容量变更；完整门禁包含探针开销。
- 软件：RX/recorder/parser `42 passed`，真实 runtime、scheduler、adapter 主机 suite
  通过，文档检查器自回归 `18 passed`。首轮 flight/resource 为 `150 passed/6 errors`：
  非阻塞测试启用设备分支，却未链接新增 recorder 依赖。改为链接真实 recorder，
  使用有界且冻结的 clk_sys 测试时钟，校验每次 service 的 runtime/dispatch 计数；
  r2 全部 `156 passed`，首轮失败日志保留。
- 构建：Release A/B/Boot 与 Flash 链接检查通过，build `20260912134107`，源码指纹
  `9314b599837a1bdba66a4680fcace52524916cfadae74406872d14e94bdb805b`，`1018` files，
  全部 `634` 个 app 翻译单元保持六容量。两 map 的 recorder 从 `176/368 B` 变为
  `224/464 B`，新增 `144 B`；链接余量 `5168 B`，本轮 `DEFAULT_MIN_FREE_BYTES`
  为 `49152 B`，正式 RAM FAIL。V4 增加探针与布局影响，不能把 V3/V4 差值当作同
  干扰条件下的速度对照。源码、镜像与主控复核见 `source-checkpoint-r1.json`、
  `build-inspection-r1.json`、`implementation-review-r1.json`。
- P3：真实 r1 run 完成四板 OTA、复位和测量，凭证为 quick diagnostic，
  `passed=true/strict_gates_passed=false`。粗 CLK 的 NO4 ARM `8`、process-image
  的 NO3 ARM `5` 和未交出运行环三项失败保留。独立 `matrix-strict-r1.json` 为
  `passed=true/diagnostic_continue=false`。一次同包 r2 resume 完成复位，但 NO4
  profile 读回 `active_level=0`，P0T 提前退出，未生成 r2 凭证；r1 保持原样。
  P3 重建后原八份构建产物 SHA 不变。
- 生命周期与恢复：r1 外层预停在 NO2 应答超时，最终 STOP 成功。r2 使用闭环工具
  内部的串行 STOP/精确代次 STOP_ACK，process-before 启动屏障超时。r3 首次 ARM
  `8` 经有界 STOP 恢复后 process-before 严格标志通过，raw-transition 首次通过，
  restored+clock-train 启动屏障超时。r4 仅复测 restored，在 NO1 ARM `5` 退出；
  拒绝时 physical armed/runtime enabled/adapter started/engine active 均为零，
  既有 map 已配置。该错误也可能来自一次性快照或 map 锁，尚未证明根因。
  helper 为此停止状态增加有界恢复准入，不跳过 map 检查。r5 保留前序链条，只重做
  restored+clock-train，首次取得严格标志通过；这是一条含失败/STOP 的有限恢复链，
  不是一次不中断全通过的生命周期试验。
- 启动观测盲点：r5 将主机 read quantum 从 `20 ms` 改为 `5 ms`，其他门禁参数
  保持。原始 startup 样本在 `2.063/3.969/5.891/7.735 s` 完成，首个有 pipeline
  fill 拒绝增长，随后三个稳定。工具因只在开始查询前检查 `6 s` 截止时间而返回
  PASS，不能据此宣称在 `6 s` 内完成。此前超时原样保留，读取粒度变化与恢复不能
  当作此盲点已修复；下一修复须校验采样完成时间并覆盖慢读取负测。
- 普通 SD：r5 四板原件全部重建，有限包对、mailbox CRC 和本地 owner 装卸审计
  通过；NO1 在采集/导出观察窗口内新增 `49` 次 transport bad，其余三板为零，
  因此原采集及 lifecycle wrapper 均失败。`capture-audit-r1.json` 的原件接受不
  覆盖窗口坏帧失败；最后一次返回码为 `TDMA_TRANSPORT_CRC_MISMATCH`。没有执行
  wrapper 后续 running-after 快照，最终 STOP 全部成功。
- 诊断计时准备：`timing-prepare-r1` 启动屏障与 NO1 周期间隔完整性失败，helper
  收尾 STOP。r2 保留同类失败，在四板仍收发、无物理故障增长/计数倒退/掉线的
  读回下，按用户已授权调试规则作有界继续，显式 `forced_continue=true`，不是
  产品准入通过。之后 `timing-r1` 完成目标 `25 s` 普通及 `90 s` 自主窗口，期间
  无 SD 操作。自主 trial `912050`、epoch `84`、期限 `180 s`，撤销至 epoch `86`
  inactive，四板 STOP 成功，无 cleanup_error。
- 完整计时：下表每个节点均使用自身同条完整峰值，单位 `µs`；有限峰值不是 WCET。

  | 节点 | 普通完整 | 自主完整 | 自主 RX handoff | 自主 overlay | 自主 dispatch | 自主 runtime publish |
  |---|---:|---:|---:|---:|---:|---:|
  | NO1 | 1472.352 | 892.916 | 281.192 | 0 | 46.012 | 25.412 |
  | NO2 | 972.664 | 1029.760 | 533.180 | 124.864 | 29.268 | 34.340 |
  | NO3 | 1053.256 | 1127.712 | 622.576 | 157.276 | 49.376 | 33.656 |
  | NO4 | 1054.716 | 1139.320 | 593.116 | 189.796 | 38.060 | 30.892 |

  自主从站 handoff 在 capture/parse 之外仍占 `102.200/54.028/66.552 µs`。
  另有未执行 capture/parse 的 last 样本达 `377.528/465.796/426.164 µs`，其中
  overlay 占 `91.040/182.192/152.104 µs`；这些记录不证明无更新稳态，差额不是
  删除某操作后的速度承诺。两窗口四板完整 WCET 均失败；自主 overrun 增量为
  `57244/59185/60535/60723`，从站观察丢弃增量为 `33252/31673/29799`。
  普通 NO1 在无 SD 计时窗口内又新增 `54` 次 transport CRC 错误，其他节点为零；
  自主四板坏帧增长均为零、RX 计数均增加。这只能定位模式差异，不能证明根因或
  自主长稳。完整父子区间检查和逐条来源见 `comparison-r1.json`。
- 自主原件：`capture-prepare-r1` 同样记录启动/普通 origin 完整性失败，在既有
  调试继续准入下准备。`capture-r1` 使用独立 trial `912052`、epoch `100`、期限
  `180 s` 的许可；四板采集/导出主机时窗为 `51.766/75.344/71.562/62.891 s`。
  这些时窗止于导出后的运行快照，批量下载可跨许可到期，不能将下载时长算成
  持续自主运行。四板原始分段重建、有限包对/CRC/本地 owner 装卸审计全部通过，
  窗口坏帧增量全零。撤销至 epoch `102` inactive，最终四板 armed/enabled/started
  全零，无 error/cleanup_error；这是独立有限窗口，不能覆盖前述普通模式失败。
- 复核封存：`review-r1.json`、`slice-manifest.json` 绑定当前源码、构建、普通/自主
  原件、计时和所有保留失败；`commit-proof.json` 记录代码/三文档分离提交与门禁。
  提交用于保存可追溯的诊断实现，不表示当前切片严格验收通过或推进产品状态。
- 下一 gate：本轮只形成有失败的诊断归因，结论 PARTIAL，`TDMA-FLIGHT-002F`
  保持 IN PROGRESS。先定位普通 origin 首个 CRC 坏帧及观察副本/物理帧/版本因果，
  修正启动截止时间取证盲点，再用现有探针优化复用时的 overlay 准入和 RX 交接。
  不继续堆叠探针或以局部余量代替完整 WCET；现有 `380 µs` 门限保持。正式 RAM、
  特等逐圈保全、blackout、同圈更新、拥塞和故障恢复仍开放，节点容量后续项后置。
  主控证据与决策入口为 `measurement-conclusion-r1.json`；registry/C11 状态不变。

### TDMA-PROGRESS-20260912-026 - RX 位反转内联与同版本计时复核

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-rbit/`。
- 实现：`tdma_pio_spi_phys_rx_ring_byte()` 在支持的 ARM/GCC 构建中用
  `__builtin_arm_rbit()` 直接反转原始 word 的位，其他目标保留 Pico `__rev()` 后备，
  仍只对 process follower 归一化并取低字节。没有引入 ACLE 同名 byte-swap 接口。
  同一 helper 服务于 header 读取、live copy 和后台发现窗口；DMA 范围准入、
  barrier、复制后 epoch/覆盖复验、latch、FIFO、wire 和资源分区不变。
  实现复核见 `implementation-review-r1.json`。
- 软件与构建：RX/recorder/parser `39 passed`，相关 flight/resource `156 passed`，
  真实 adapter 主机 suite 通过，文档检查器自回归 `18 passed`。已有复制向量覆盖
  全字节值、位偏移、未对齐目的地址哨兵、SRAM/序号回绕和准确读取次数；推进中的
  DMA 覆盖、epoch 与取消回归继续通过。主机执行后备分支，设备分支由真实 ARM
  镜像指令及当前固件硬件复核覆盖。Release A/B/Boot 与 Flash 链接检查通过，build
  `20260912124550`，源码指纹
  `7c254e48d3587b91767cbf4cb2d440da91e5c35221f598b6abbddd63814d1143`，
  `1018` files，全部 `634` 个 app 翻译单元使用六容量。两镜像的 ring-copy 函数
  均内联 RBIT 且不再调用外部函数；归一化/header 路径也不再调用外部 `__rev`。
  V3 探针和 recorder `176/368 B` 不变，链接余量仍为 `5312 B`，未新增 RAM；
  `DEFAULT_MIN_FREE_BYTES` 本轮为 `49152 B`，正式 RAM FAIL。原始反汇编与 map
  引用见 `build-inspection-r1.json`，不据指令数宣称硬件时延上界。
- P3 与矩阵：真实 `p3-r1 run` 完成四板 OTA/复位/测量，凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`、`passed=true/strict_gates_passed=false`。
  粗时钟 NO3 `ARM result=8`、编码标记、TRN-00 驻留矩阵及 process-image startup
  barrier timeout 四项严格失败保留。TRN-00 trial 0 报告字段数 `49`、期望 `48`，
  造成部分驻留与 loop 证据缺失；独立 `matrix-command-r1` 退出 `1`，没有生成可用
  矩阵，也没有进入依赖生命周期。一次同包 `p3-r2 resume` 复位重测后，粗时钟、
  编码标记与驻留矩阵通过；独立 `matrix-strict-r2.json` 为
  `passed=true/diagnostic_continue=false`。r2 的 startup barrier timeout 仍保留，
  不把恢复成功当作这些异常已修复。默认源码凭证原样采用真实 run 的 r1，恢复证据
  单独绑定，P3 重建后的原构建 SHA 已核对一致。
- 当前源码闭环：严格矩阵通过后，`lifecycle-r1` 的 process-before、raw-transition、
  process-restored+clock-train 均首次严格通过，无 diagnostic continue。普通四板
  SD 原件重建、包对身份、mailbox CRC 和本地 owner 装卸边界通过，坏帧计数零增长，
  最终 STOP 成功，无 error/cleanup_error。
- 计时：`timing-r1` 完成目标 `25 s` 普通窗口，再以 trial `912040`、epoch `66`、
  期限 `180 s` 的许可完成目标 `90 s` 自主窗口，期间无 SD 操作，撤销与 STOP 成功。
  普通完整峰值按 NO1..NO4 为 `1246.544/932.340/1010.868/959.268 µs`；从站
  ring copy 为 `177.852/200.708/265.348 µs`，相对前轮分别约 `-29.49%/-12.27%/+2.54%`。
  自主模式各自同条完整峰值的对照如下：

  | 节点 | 上轮完整 µs | 本轮完整 µs | 上轮 ring copy µs | 本轮 ring copy µs |
  |---|---:|---:|---:|---:|
  | NO1 | 855.444 | 875.236 | 0 | 0 |
  | NO2 | 1093.168 | 975.408 | 239.392 | 114.628 |
  | NO3 | 1089.604 | 1052.828 | 266.020 | 198.440 |
  | NO4 | 1104.808 | 1055.644 | 254.744 | 186.736 |

  自主从站复制子项约下降 `52.12%/25.40%/26.70%`，完整峰值只下降约
  `10.77%/3.38%/4.45%`；NO1 完整峰值上升约 `2.31%`。两轮均为 V3，但仍是独立
  有限窗口，代码/寄存器/缓存布局及运行干扰可能变化，不能将全部差值归因于本修改。
  自主从站同条完整记录在 RX_CAPTURE 之外仍有约 `573/605/545 µs`；该差额不是
  移除 RX 后的性能承诺，说明只优化复制仍不足以闭合完整门禁。两窗口 RX 计数增加，
  transport/schedule/profile 坏帧零增长；自主 scheduler overrun 按 NO1..NO4 增加
  `53909/59059/59650/58849`，四板完整 WCET 全 FAIL。从站观察丢弃增加
  `33294/31613/29586`，不能代替逐圈特等保全。origin 专用路径不报告该复制子项，
  零值不代表无 RX 成本；父子包含式不能重复求和或拼接最大值，完整门禁不扣探针
  成本，有限峰值不是 WCET 上界。对照与下一动作见 `comparison-r1.json`、
  `measurement-conclusion-r1.json`。
- 采集准备及保留失败：`capture-prepare-r1/r2` 的外层并行 STOP 分别在 NO4/NO2
  应答超时，两次状态读回均已停止，且各自最终 STOP 全部返回 OK。随后用闭环工具
  自带的串行 STOP、精确配置代次 STOP_ACK、配置及 ARM 完成准备，不修改固件
  STOP 语义或把超时改写为成功。r3 使用严格配置并通过，但临时许可要求
  `TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE`；`capture-r1` 的 trial `912042` 因准备模式
  不满足前置条件而未取得许可，数值返回读超时，后续错误队列为 `-200 Execution error`。
  尚未开始采集即撤销并停止，旧 trial 保持 inactive。这是执行参数错误，不能据此
  宣称线路丢包。r4 本地 helper 的相对/绝对路径引用失败，在硬件命令前退出；路径
  归一化后 r5 严格准备通过。确认许可准入条件后，r6 显式使用调试配置完成同一
  STOP/ACK 流程并通过，诊断属性原样保留。所有失败目录、命令返回和后继状态保留。
- 自主原件：`capture-r2` 使用新 trial `912044`、epoch `106` 的独立有限许可；
  四板采集/导出在许可命令发出后主机单调时钟约 `47.484/65.719/59.234/53.172 s`
  完成，均在本次 `180 s` 窗口内。这是主机时窗证据，批量下载可跨过许可到期，
  不将下载时长记为持续自主运行。原件重建、包对身份、mailbox CRC 和本地 owner
  装卸审计全部通过，坏帧计数零增长。许可撤销至 epoch `108`、inactive，四板最终
  physical armed/runtime enabled/adapter started 均为零，无 error/cleanup_error。
  前一计时许可撤销至 epoch `68`，失败的采集许可尝试收尾为 epoch `82`、inactive；
  独立有限窗口不能拼成连续长稳或逐圈特等保全证据。
- 复核封存：`review-r1.json` 与 `slice-manifest.json` 绑定源码、构建、计时、普通/
  自主原件及保留失败，代码与三文档分开提交，门禁与提交 SHA 见 `commit-proof.json`。
  本切片结论 PARTIAL，不把诊断 P3、准备重试或局部计时改善提升为产品验收通过。
- 下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS，现有 `380 µs` 门限保留。
  优先归因剩余 owner/adapter 工作和运行干扰，再选择迁移或削减的操作；完整 WCET、
  正式 RAM、特等逐圈保全、blackout、同圈更新、拥塞和故障恢复仍开放，节点容量
  后续项后置，registry/C11 状态不变。

### TDMA-PROGRESS-20260912-025 - RX 取帧内部归因与复制优先级

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-acquire-attribution/`。
- 变更：`TDMA_SERVICE_TIMING_VERSION` 从 V2 升至 V3，追加 DMA 观察、候选定位、
  固定 header 校验和 ring copy 四子项，包含于 `TDMA_TIMING_RX_ACQUIRE`。初始和
  复制后 DMA 观察累计计数；live 帧与后台发现私有窗口都报告复制，依调用次数及
  定位/header 区分。主机明确支持 V1/V2/V3，未知版本或长度不匹配拒绝。探针包裹
  原操作，保留 header 短路顺序、已完成范围准入、barrier、复制后 epoch/覆盖复验及
  latch 因果边界；FIFO、wire、资源、节点容量和预算不变。实现复核见
  `implementation-review-r1.json`。
- 软件与构建：RX/recorder/parser `39 passed`，相关 flight/resource `156 passed`，
  真实 adapter 主机 suite 通过，文档检查器自回归 `18 passed`。真实 recorder 与
  推进中的 DMA 总线夹具验证 discovery/live 区分及复制被覆盖拒绝后的计时保留。
  首次 RX 测试因新夹具未调用包含的取消 helper，被 `-Werror=unused-function` 拒绝，
  保留 `1 failed/38 passed`；补充实际取消收尾断言后通过，未放宽告警。Release
  A/B/Boot 与 Flash 链接检查通过，build `20260912120749`，源码指纹
  `52f0dd17fb52b8a2fd73408f2d11130a4b48840ec27fd80ccd5b0812e59eb301`，
  `1018` files，全部 `634` 个 app 翻译单元使用六容量。recorder work/snapshot
  分别从 `144/304 B` 增至 `176/368 B`，合计新增 `96 B`，两镜像链接余量从
  `5408 B` 降至 `5312 B`；`DEFAULT_MIN_FREE_BYTES` 本轮为 `49152 B`，正式 RAM FAIL。
- P3 与当前源码闭环：真实 `p3-r1 run` 完成四板 OTA/复位/测量，凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true/strict_gates_passed=false`。
  粗时钟 NO3 `ARM result=5` 与 P3 process-image startup barrier timeout 两项严格
  失败保留。编码标记、P0T、链路与 TRN00/01/02 通过。独立 `matrix-strict-r1.json`
  首次 `passed=true/diagnostic_continue=false`，无需 P3 恢复重跑。
  `lifecycle-r1` 的 process-before、raw-transition、process-restored+clock-train
  均首次严格通过；普通四板 SD 原件重建、包对身份、mailbox CRC 和本地 owner
  装卸边界通过，坏帧计数零增长，最终 STOP 成功且无 cleanup_error。
- 计时：`timing-r1` 完成目标 `25 s` 普通窗口，随后以 trial `912036`、epoch `66`、
  期限 `180 s` 的许可完成目标 `90 s` 自主窗口，期间无 SD 操作。普通完整峰值按
  NO1..NO4 为 `1196.152/984.032/1037.704/1069.648 µs`；自主完整峰值为
  `855.444/1093.168/1089.604/1104.808 µs`。自主从站各自同条完整峰值的取帧分解：

  | 节点 | 取帧 µs | DMA 观察 µs | 定位 µs | header µs | ring copy µs | 未分配余项 µs |
  |---|---:|---:|---:|---:|---:|---:|
  | NO2 | 391.688 | 51.008 | 15.416 | 47.152 | 239.392 | 38.720 |
  | NO3 | 429.128 | 30.628 | 20.076 | 85.508 | 266.020 | 26.896 |
  | NO4 | 423.876 | 42.636 | 18.156 | 59.352 | 254.744 | 48.988 |

  每条记录 DMA 观察调用两次，其余三子项各一次。ring copy 占取帧约 `60–62%`，
  定位约 `4–5%`；普通从站 ring copy 为 `252.244/228.788/258.780 µs`，方向一致。
  两窗口 RX 计数增加且 transport/schedule/profile 坏帧零增长；自主 scheduler
  overrun 按 NO1..NO4 增加 `53791/58727/57473/57113`，完整 WCET 全 FAIL。
  自主从站观察丢弃增加 `32955/30801/29459`，不能证明特等时间戳逐圈保全。
  自主 origin 专用路径不报告新子项，零值不代表零成本；V3 增加探针和布局开销，
  与前轮 V2 不是同条件速度对照。父子包含式不能重复相加或拼接独立最大值，
  完整 phase 门禁不扣探针成本，有限峰值也不是 WCET 上界。
- 下一修改依据：`acquire-assembly-r1.json` 绑定实际镜像反汇编。复制循环每个原始
  word 调用外部 `__rev`，其实际实现为 `rbit` 后返回；`tdma_rx_scan_locate()` 虽有
  两次长除法调用，实测占比较小。优先验证内联同语义位反转；注意 ACLE 同名 `__rev`
  表示 byte swap，不能按名称直接替换。下一切片保持 V3 计时，先测试语义和镜像指令，
  再完成当前源码硬件闭环和实测，不提前承诺收益；本切片未修改复制算法。
- 自主原件与收尾：准备首次通过，`capture-r1` 使用独立 trial `912038`、epoch `82`、
  期限 `180 s` 的许可。四板采集/导出在授权命令发出后主机单调时钟约
  `44.610/59.141/52.047/50.922 s` 完成；下载可跨过许可到期，不将下载时长算作持续
  自主运行。原件重建、包对身份、mailbox CRC 与本地 owner 装卸审计全部通过，坏帧
  计数零增长。计时与采集分别撤销至 epoch `68/84`、inactive；两次最终四板
  physical armed/runtime enabled/adapter started 均为零，无 error/cleanup_error。
- 复核封存：`review-r1.json`、`slice-manifest.json` 绑定源码、构建、原件与命令 SHA；
  门禁与代码/三文档分离提交见 `commit-proof.json`。结论 PARTIAL，保留首次主机
  测试失败、真实 P3 两项严格失败和正式 RAM FAIL，不提升为产品通过。
- 下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS，现有 `380 µs` 门限保留；当前
  差距不能靠适度放宽解决。先验收 ring copy 位反转内联，再调查剩余 owner/adapter
  成本；正式 RAM、完整 WCET、特等逐圈保全、blackout、同圈更新、拥塞和恢复缺口
  继续开放，节点容量后续项后置，registry/C11 状态不变。

### TDMA-PROGRESS-20260912-024 - overlay 静态授权预计算

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，
  非事实源；证据根为 `out/HardwareAcceptance/20260912/tdma-flight-overlay-layout/`。
- 变更：`tdma_flight_engine_activate()` 在现有 map writer guard 下预计算固定本地
  mailbox 的唯一完整授权；`tdma_flight_engine_copy_tx_layout()` 运行时只读取 slot、
  generation 和 mask，发布中、版本变化或停用立即拒绝。成功换配置清除缓存，换本地
  owner 重新计算；一般 legacy map 的激活准入不变，不适合 compact overlay 的 map
  得到零授权。FIFO 消费/复用计数、物理 grant/commit/selection、wire、资源、节点容量
  和计时版本均不变。源码复核见 `implementation-review-r1.json`。
- 软件与构建：真实 adapter/overlay、FIFO、service scheduler 主机测试通过，覆盖
  所有固定 wire slot、停用/重激活、active 配置失败、发布中读取、部分 mailbox、移除
  写权限和重复 owner span。相关 flight/计时/资源回归 `165 passed`，文档检查器
  自回归 `18 passed`。Release A/B/Boot 与 Flash 链接检查通过，build
  `20260912112529`，源码指纹
  `c95aa9b43ead3d30e65fa8c72e6de61b38af86ef86e849deff0c689f265f935a`，
  `1017` files，全部 `634` 个 app 翻译单元使用六容量。新增字段被外层对象的对齐空间
  吸收，两镜像 owner 大小仍为 `12056 B`，链接余量仍为 `5408 B`；
  `DEFAULT_MIN_FREE_BYTES` 本轮为 `49152 B`，正式 RAM FAIL 保留。map 与实际访问器
  反汇编见 `build-inspection-r1.json`，指令形态不能代替硬件时延。
- P3 与严格矩阵：真实 `p3-r1` 完成四板 OTA/复位/测量，凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true/strict_gates_passed=false`。
  粗时钟 NO3 `ARM result=8`、编码标记失败、SCK 候选覆盖不足和 process-image
  startup barrier timeout 保留；独立严格矩阵首次退出 `1`，未产生结果文件，也未
  进入依赖生命周期。一次同 build `p3-r2 resume` 复位重测后，独立
  `matrix-strict-r2.json` 退出 `0`、`passed=true/diagnostic_continue=false`。
  r2 的粗时钟 NO1 `ARM result=5`、编码标记失败与 process-image NO4 `ARM result=5`
  仍保留。默认源码凭证原样采用真实 `run` 的 r1，恢复证据单独绑定，不替代原始失败。
- 当前 build 闭环：确认严格矩阵通过后，`lifecycle-r1` 的 process-before、
  raw-transition、process-restored+clock-train 均首次
  `passed/closed_loop_passed/realtime_gate_passed=true`、`diagnostic_continue=false`。
  普通四板 SD 原件重建、包对身份、mailbox CRC 与本地 owner 装卸边界全部通过，
  原件期间坏帧计数零增长；本次最终 STOP 成功，不代表历史 STOP 缺陷已闭合。
- 计时：`timing-r1` 完成目标 `25 s` 普通窗口，随后以 trial `912032`、epoch `66`、
  期限 `180 s` 的许可证完成目标 `90 s` 自主窗口，期间无 SD 下载，撤销和 STOP 成功。
  普通完整峰值按 NO1..NO4 为 `1218.124/953.812/1016.808/967.384 µs`。
  自主完整峰值及各自同条 overlay 子项如下；origin 不走 follower overlay，零值
  不能解释为无接收或装卸成本。

  | 节点 | 上轮完整 µs | 本轮完整 µs | 上轮 overlay µs | 本轮 overlay µs |
  |---|---:|---:|---:|---:|
  | NO1 | 902.872 | 869.044 | 0 | 0 |
  | NO2 | 1004.324 | 1040.240 | 141.840 | 137.944 |
  | NO3 | 1080.560 | 1081.272 | 197.404 | 177.472 |
  | NO4 | 1083.240 | 1026.104 | 145.464 | 114.484 |

  自主完整峰值变化约 `-3.75%/+3.58%/+0.07%/-5.27%`；overlay 子项虽下降，
  完整耗时仍有升有降，独立有限窗口也不能证明差值全部来自本修改。两窗口 RX 有
  进展，transport/schedule/profile 坏帧计数零增长，但 scheduler overrun 仍增加，
  四板完整 WCET 均 FAIL。自主从站观察丢弃增加 `32897/30888/29108`，不能代替
  特等时间戳逐圈无损保全。同条从站记录中 RX_CAPTURE 之外仍有约 `438–550 µs`，
  相减不是移除 RX 后的性能承诺。父子区间包含式，不能相加或拼接独立最大值。
  对照与限制见 `comparison-r1.json`、`measurement-conclusion-r1.json`。
- 自主原件与收尾：采集准备首次通过，`capture-r1` 使用独立 trial `912034`、
  epoch `82`、期限 `180 s` 的许可。主机单调时钟显示四板采集/导出在授权命令发出后
  约 `43.079/57.438/50.719/47.860 s` 完成；这是主机时窗证据，不冒充硬件同钟证据。
  批量下载可在许可到期后完成，不据下载结束时间扩展自主运行结论。四板 SD 原件
  重建、包对身份、mailbox CRC 和本地 owner 装卸边界全部通过，坏帧计数零增长。
  最终撤销至 epoch `84`、inactive，四板 physical armed/runtime enabled/adapter
  started 均为零；前一计时许可撤销至 epoch `68`。两次有限授权不能拼成连续长稳证据。
- 复核封存：`review-r1.json` 与 `slice-manifest.json` 绑定源码、构建、命令结果和
  原始证据 SHA；代码与文档分开提交，门禁和提交 ID 见 `commit-proof.json`。
  切片结论 PARTIAL，保留真实 P3 两轮的严格失败、首次独立矩阵失败及正式 RAM FAIL。
- 下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS，现有 `380 µs` 门限保留。
  当前差距不能靠适度放宽解决；继续拆分 live RX 与 owner/adapter 交接成本、调查
  干扰，并评估保留 FIFO 语义的无更新路径。正式 RAM、完整 WCET、逐圈特等保全、
  service blackout 与同圈多 owner 证据分别闭合，动态节点后续项继续后置。

### TDMA-PROGRESS-20260912-023 - 紧凑 RX 副本与共享相邻字节复制

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，
  非事实源。证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-copy/`。
- 变更：Core1 私有归一化帧改为对齐的字节数组，DMA word ring 不变；
  `tdma_pio_spi_phys_rx_ring_copy()` 共享相邻字节，错位复制长度 N 时只读 N+1 个原始字，
  无错位只读 N 个。legacy、Core0 扫描工位输入与当前 live 帧沿同一 helper 复制；
  完整区间准入、复制后 epoch/覆盖复验、锁定的 wire 相位与 latch/RTT 绑定顺序不变。
  已验证帧长与容量后使用 memcpy 交入 packet；计时版本不变，未改生命周期、资源、
  wire 布局、编译节点容量或预算。代码和文档分别提交，提交与门禁见 `commit-proof.json`。
- 软件与构建：真实 scanner 的全部位相位、两种 ISR 方向、字节值、非对齐目标哨兵、
  零长、SRAM/序号回绕、DMA 覆盖/epoch、取消及 wire 相位负测继续通过。
  RX/计时/bit-pipeline `79 passed`，相关 flight 回归 `132 passed`，文档检查器
  自回归 `18 passed`。flight 首次回归因独立夹具漏提取新 helper 失败，补齐真实函数
  与字节缓冲后通过，原 `flight-tests-r1.log` 保留。
  Release A/B/Boot、Flash 链接检查通过，build `20260912104508`，源码指纹
  `601abc9187498bbb80d1b12108e6b4ec8f93313734cc2c21491fc0472eed84c6`，
  `1017` files，全部 `634` 个 app 翻译单元使用六容量。两镜像的私有帧均由
  `1184 → 296 B`，链接余量 `4520 → 5408 B`，实际释放 `888 B`；正式要求由
  `DEFAULT_MIN_FREE_BYTES` 定义，本轮仍为 `49152 B`，RAM FAIL 保留。
  `build-inspection-r1.json` 核对 map 与反汇编，代码/数据布局变化不能直接当作时延收益。
- P3 与闭环：真实 `p3-r1` 完成四板 OTA、复位、链路和训练测量；凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK NO2 `ARM result=8`、process-image NO4 `ARM result=5` 和未交接运行环
  的失败保留。独立 `matrix-strict-r1.json` 首次严格通过，确认成功后才运行生命周期。
  `lifecycle-r1` 的 process-before、raw-transition、process-restored+clock-train
  均首次 `passed/closed_loop_passed/realtime_gate_passed=true`，`diagnostic_continue=false`；
  四板 SD 原件重建、包对身份、mailbox CRC 与本地 owner 装卸边界均通过。
  最终 NO3 STOP 应答超时，因此生命周期包装命令退出 `1`、保留 `cleanup_error`；
  四板读回已停，一次原超时参数的 STOP 重发通过。`stop-recovery-scope-r1.json`
  区分三段功能通过与收尾失败，恢复不将原包装命令提升为全程成功。
- 计时：`timing-r1` 先完成目标 `25 s` 普通窗口，再签发 trial `912028`、epoch `66`、
  期限 `180 s` 的许可，完成目标 `90 s` 自主窗口；窗口内不执行 SD ARM/SAVE/download，
  最终撤销与四板 STOP 成功。普通完整记录峰值按 NO1..NO4 为
  `1221.272/927.764/1051.148/1038.900 µs`。自主完整记录峰值及其同条子项如下；
  origin 专用 RX 不报告这些子项，零不能解释为无接收成本。

  | 节点 | 上轮完整 µs | 本轮完整 µs | 本轮取帧 µs | 本轮包复制 µs | 本轮时间换算 µs | 本轮 latch µs |
  |---|---:|---:|---:|---:|---:|---:|
  | NO1 | 842.792 | 902.872 | 0 | 0 | 0 | 0 |
  | NO2 | 1082.260 | 1004.324 | 391.204 | 14.984 | 14.972 | 25.184 |
  | NO3 | 1236.568 | 1080.560 | 446.988 | 4.592 | 19.732 | 26.284 |
  | NO4 | 1260.060 | 1083.240 | 453.552 | 6.276 | 14.284 | 60.068 |

  从站自主完整峰值下降约 `7–14%`，包复制由上轮同类完整记录的
  `79.836/98.776/71.556 µs` 缩短；NO1 完整峰值上升约 `7%`，NO2 取帧也上升，
  不能宣称所有路径稳定加速。两轮均使用同版本计时，但属于独立有限窗口，存在布局
  与干扰差异；表中子项来自各自完整峰值，不是可拼接的独立最坏值。
  两个窗口 RX 均有进展，transport/schedule/profile 坏帧计数零增长，调度 overrun
  仍增长、所有节点完整 WCET 均 FAIL。自主从站观察丢弃增加
  `33748/31865/30016`，普通可丢弃镜像不能代替特等时间戳逐圈无损保全。
  同条自主从站记录中，RX_CAPTURE 之外仍有约 `492–533 µs`；该相减值不能作为
  删除 RX 后的性能承诺。明细见 `comparison-r1.json` 与 `measurement-conclusion-r1.json`。
- 自主原件：采集准备 `r1` 的初始 NO2 STOP、`r2` 的初始 NO3/最终 NO1 STOP
  应答超时保留，四板快照均已停止；一次有界软件复位后 `capture-prepare-r3` 成功。
  `capture-r1` 使用独立 trial `912030`、epoch `14`、期限 `180 s` 的许可；按主机
  单调时钟，四板采集/导出在发出授权后约 `42–57 s` 内完成，下载可在许可到期后
  结束，不再执行到期后的自主计时。四板原件重建、包对身份、mailbox CRC 和本地
  owner 装卸审计全部通过，原采集错误计数零增长；最后撤销至 epoch `16`、inactive，
  四板 physical armed/runtime enabled/adapter started 均为零。前一计时许可撤销至
  epoch `68`；中间存在软件复位，epoch 不跨启动比较，两窗口也不能拼成连续运行证据。
- 复核与封存：`review-r1.json`、`slice-manifest.json` 记录当前源码、构建和原件 SHA，
  切片结论为 PARTIAL。主机 map 提取正则首次失败、对照报告与包装记录重名失败、
  默认 staged 凭证未同步的首次门禁失败分别保留；修复工具副本、复核既有输出并将
  本轮真实 `run` 凭证原样同步后继续，未覆写旧证据或手工修改验收结论。
- 下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS，当前 `380 µs` 门限保留。继续
  细分 live ring 取帧与 owner/adapter 交接，核实复制、计数/提示/复验、布局和干扰，
  再选择有界优化；正式 RAM、完整 WCET、STOP/准入问题、特等逐圈保全、service
  blackout 和同圈多 owner 仍需独立闭合。动态节点保持后置，registry/C11 状态不变。

### TDMA-PROGRESS-20260912-022 - RX 内部分项计时与 Core1 预算复评

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验/评估快照，
  非事实源。证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-attribution/`。
  用户授权结合装卸成本与特等席稳定余量评估适度调整门限；本轮保持预算和节点容量不变。
- 归因边界：Release 反汇编证明 SDK `__rev` 是 `rbit`，host 桩语义一致，未据外部调用
  猜测其为瓶颈。`TDMA_SERVICE_TIMING_VERSION` 追加 RX 取帧、包复制、时间换算、
  latch 读取/重装子项，父子区间包含式，不能相加；origin 专用路径不报告这些子项。
  SCPI 自动输出同一 enum 的版本和长度，主机解码器兼容旧版并拒绝未知/错长 schema。
  recorder 的 Core1 单 writer、短 seqlock、边界 reset 与完整 peak 记录不变。
- 软件：计时/schema 与 RX/bit-pipeline 回归 `78 passed`，相关 flight、生命周期、
  准入与 schedule 回归 `132 passed`。首次构建因物理层漏包含计时头文件失败，补齐后
  Release A/B/Boot 和 Flash 链接检查通过，失败日志 `build-r1.log` 保留，成功记录
  为 `build-r2.json`。build `20260912095527`，源码指纹
  `6b93be59a500f27c8a47723702ffb260408dccf0ef3077f8dfeda3cab6ec1d45`，
  `1017` files，全部 `634` 个 app 翻译单元仍使用六容量。构建产物 SHA-256 见
  `source-checkpoint-r1.json`。记录器主 SRAM 增加 `96 B`，链接余量
  `4616 → 4520 B`；正式要求由 `DEFAULT_MIN_FREE_BYTES` 定义，本轮 `49152 B`，
  `ram-r1.log` 保留 FAIL。
- P3：`p3-r1` 完成四板 OTA、复位和四链路双组测量，凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK NO2 `ARM result=5`、coded-marker、SCK 候选覆盖/重装余量及启动 barrier
  失败保留。SCK link 1 的八次有效读数仅覆盖 offset `1`，未满足两个候选要求；
  选中行最小重装余量为 `-1`。严格矩阵构造拒绝，未生成 `matrix-r1.json`；主控
  误发后继生命周期命令在缺少配置处失败，未进入运行，初始/最终 STOP 均成功。
  `calibration-recovery-scope.json` 区分该编排失败与板端失败，不将其计为有效硬件试验。
  一次同固件 `p3-r2` 恢复重新测量后，独立 `matrix-r2.json` 严格通过，自动 row `8`；
  恢复 P3 中 coded-marker 和启动 barrier 失败仍保留，不覆盖初始 run 凭证。
- 生命周期：`lifecycle-r2` 的 process-before、raw-transition、带 clock-train 的
  process-restored 均首次严格通过，`passed/closed_loop_passed/realtime_gate_passed=true`、
  `diagnostic_continue=false`。四板 SD 原件重建、有限包对身份、mailbox CRC 和局部
  owner 装卸边界均通过，transport/schedule/profile 错误零增长。
- 试验恢复与原件：使用 `256 B` 下载页，普通和自主两批四板 SD 原件均首次下载成功。
  计时准备前，`trial-prepare-r1` 的 NO4 STOP、`trial-prepare-r2` 初始 NO1/最终 NO3
  STOP 应答超时保留；四板读回已停，一次有界复位后 `trial-prepare-r3` 成功。
  `trial-r1` 的授权 epoch `14`、期限 `360 s`；采集/下载耗时约 `363.926 s`，
  后置自主计时首条记录晚于到期拍，NO1 physical armed/adapter started 为零，
  所以原 trial 退出码仍为失败。SD 原件审计全通过不能抹去计时失败；撤销后 epoch
  `16`、inactive，四板 STOP 成功。独立 `trial-r2` 使用 epoch `30`、期限 `180 s`，
  不执行 SD ARM/SAVE/download，完成目标 `90 s` 自主计时；撤销后 epoch `32`、inactive，
  最终四板 physical armed/runtime enabled/adapter started 均为零。两窗口属于不同
  授权 epoch 的同源码证据，不能拼成单个连续无损运行窗口。
- 计时：普通目标 `25 s` 的完整记录峰值按 NO1..NO4 为
  `1223.220/1070.984/1304.272/1238.160 µs`。自主完整峰值及其同条记录内的 RX 子项如下；
  origin 专用路径不报告这些 RX 子项，表中的零不代表没有接收成本。

  | 节点 | 完整 phase µs | RX 取帧 µs | 包复制 µs | 时间换算 µs | latch µs |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 842.792 | 0 | 0 | 0 | 0 |
  | NO2 | 1082.260 | 341.796 | 79.836 | 20.660 | 63.284 |
  | NO3 | 1236.568 | 509.784 | 98.776 | 12.556 | 43.288 |
  | NO4 | 1260.060 | 613.536 | 71.556 | 18.624 | 26.400 |

  自主 follower 的取帧占 RX_CAPTURE 约 `61–81%`，该区间还包含 DMA 观察、提示处理、
  live/private copy 和复验，不能归结为单纯 memcpy。包复制是额外逐字到逐字节转换；
  Release 循环本体是 load/store/compare/branch，没有函数调用或显式 wire wait。
  剩余 CPU、存储访问/干扰成本需继续验证，未仅凭 `__rev` 外部调用定位根因。
  两个有效计时窗口均有 RX 进展、transport/schedule/profile 错误零增长，但所有节点
  完整 phase 仍超 `380 µs`；同条自主 follower 记录的 RX_CAPTURE 之外仍有约
  `506–538 µs`。这些是包含计时开销的有限记录，不是 WCET 上界或删除 RX 后的性能承诺。
  `attribution-r1.json` 保留完整同条分解与前轮对照，不把本轮计时布点当作性能优化。
- 门限评估：事实源为 `PROJECT_CORE1_CYCLE_CYCLES`、`PROJECT_CORE1_PHASE_TDMA_*`、
  `PROJECT_CORE1_TDMA_SOFTWARE_MARGIN_CYCLES` 和板级时钟。当前快照每周期 `1000 µs`，
  TDMA 窗口 `400 µs`、WCET `380 µs`，下一 VDC 从窗口末端开始；全表声明执行预算合计
  `959.2 µs`。旧串行最大 wire `245.6 µs` 加声明软件预留 `120 µs` 为 `365.6 µs`，
  仍不能把声明预留当作装卸实测。自主硬件与 CPU 可重叠，不能再次从 CPU 预算扣除
  用户约 `240 µs` 物理飞行时间。以该典型圈周期估算，每个调度周期最多跨过约五圈；
  若每圈处理 `140 µs`，同拍约需 `700 µs`。特等席需硬件逐圈保全、固定队列及有界
  收割，容量由最短圈周期和最长消费停顿加边界/在途余量确定，不能以典型值代替上界。
  `390 µs` 候选仅余 `10 µs` 起始/收尾空间，必须测得足够余量才可考虑；`400 µs`
  耗尽窗口余量，更大的值须整体重排 profile/phase 并复核其他 mandatory 负载及 C11。
  当前保留 `380 µs`，详细模型和候选见 `budget-assessment-r1.json`、`schedule-r1.svg`。
- 收尾：本轮功能取证闭合，切片验收结论为 PARTIAL。源码复核及成功/失败证据入口为
  `implementation-review-r1.json`、`review-r1.json`、`slice-manifest.json`；代码与
  文档分离提交及门禁证据见 `commit-proof.json`。`TDMA-FLIGHT-002F` 保持 IN PROGRESS，
  正式 RAM、完整 WCET、校准/STOP 应答问题、逐圈特等席、service blackout 和同圈多
  owner 门禁仍开放。下一候选是压缩 CPU 私有 RX 帧的表示并去掉逐字转换，继续归因
  live ring 取帧与 owner 交接；必须保留 DMA word ring、epoch/覆盖复验和 wire/latch
  边界，并重新完成对应源码的软硬件验收。动态节点后置，registry/C11 不变。

### TDMA-PROGRESS-20260912-021 - 原始 DMA 候选发现异步工位与当前切片验收

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260912/tdma-flight-async-scan/`。按用户要求先
  完成本切片验收；未继续调整节点容量，也未操作 NO5 或另一台设备的单板工作。
- 实现：独立 `tdma_rx_scan_t` 保存至多 `TDMA_RX_SCAN_WINDOW_BYTES` 的已完成
  DMA 字节副本，Core0 用真实 transport CRC 解码器发现几何位置、bit shift、帧长
  和连续帧证据。Core1 直接定位当前完整帧、重新复制 live ring，并复验覆盖与
  observation epoch；原 RX 工位仍在 live copy 后绑定当时 latch/RTT，后台完成
  时刻不充当接收时刻。tail 独立建模，不能从最大 payload 长度反推实际帧间距。
  工位忙时不等待、不覆盖输入；旧请求、persona 或配置提示拒绝。STOP/训练/origin
  切换取消提示，被取消 worker 只归还私有工位，重新 ARM 等待取消 ACK。parser
  重同步不能改变已锁定 overlay 相位；维护路径保留原扫描器。
- 软件：真实 legacy/async DMA 捕获共 `25` 项，相关 flight 回归共 `150 passed`。
  覆盖所有 bit shift、ISR 方向、跨环回绕、worker 暂停后读新帧、复制时覆盖和 epoch
  变化、取消 ACK、CRC 假头、陈旧配置提示及容量越界。补充资源、PIO、NO5 隔离、
  调度和计时回归中 `77 passed / 7 failed`，失败来自预算工具未跟随共享头文件
  常量位置；补入 `CONFIG_PATHS` 后调度/计时 `8 passed`，原失败保留。
- 构建：Release A/B/Boot 及 Flash 链接检查通过；build `20260912091130`，源码
  指纹 `75dcc6c58fed028c7973f13ebd2a40a9fd6ee99948b968b868b6de29eae8fd83`，
  `1016` files，全部 `634` 个 app 翻译单元仍使用六容量。摘要和产物 SHA-256 见
  `source-checkpoint-r1.json`。扫描工位占 `704 B`，物理对象增加 `40 B`，主 SRAM
  净增 `744 B`，链接余量 `5360 → 4616 B`；正式要求仍由
  `DEFAULT_MIN_FREE_BYTES` 定义，本轮为 `49152 B`，`ram-r1.log` 保留 FAIL。
- P3：四板 OTA 和四链路双组测量通过。凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK 的 NO4 `ARM result=8`、首次 process-image 的 NO3 `result=8` 及未
  handoff running 保留失败。本轮 TRN00/01/02 独立生成严格 `matrix-r1.json`，
  `passed=true`、`diagnostic_continue=false`；自动 row `3` 的 DATA 为 `[5,5,5,5]`。
  本次自动行成功不消除前轮自动选行/CRC 失败，也没有复用旧 build 矩阵。
- 生命周期：`lifecycle-r1` 的过程映像通过，raw 切换在 NO1 flight-map 准入
  `result=5` 处拒绝，发生在物理 ARM 前；`lifecycle-r2` 的 NO3 同类拒绝保留。
  第二轮 STOP 响应有一次超时，后续四板 physical/runtime/adapter 停止快照均为零，
  不能用状态读回抹去串口超时。原始动作、失败快照和有界恢复见
  `lifecycle-rejection-r1.json`、`reset-recovery-r1.json`。软件复位后
  `lifecycle-r3` 先保留 NO2 `result=8` 及 STOP 恢复，随后 process-before、
  raw-transition、带 clock-train 的 process-restored 均严格通过，
  `passed/closed_loop_passed/realtime_gate_passed=true`、`diagnostic_continue=false`。
- 原始采集：`lifecycle-r3` 四板 SD 片段重建、有限包对身份、mailbox CRC 与局部
  owner 装卸边界全部通过，采集期间 transport/schedule/profile 错误零增长。
  `trial-r1` 的自主采集在 NO2/NO4 下载时出现 non-hexadecimal 响应，原 summary
  和整体 trial 退出码仍为失败；两板原运行快照的 TDMA 错误增量均为零。STOP 后以
  较小串口页重读同一批已导出文件，核对 build、capture tag/sequence 与状态完全
  不变，未重做 ARM/SAVE；`recovered/` 保留重读日志。自主四板原件均可重建，
  有限包对身份、mailbox CRC 与装卸边界通过，但重读不能消除首次串口下载失败。
- 计时：`trial-r1` 使用有限期 trial `912022`，普通采集目标 `25 s`、自主目标
  `90 s`，实际命令、样本与完整 profile 记录均保留。普通完整峰值按 NO1..NO4 为
  `1243.000/1015.344/1156.704/1170.200 µs`；自主为
  `869.380/1049.844/1227.028/1260.388 µs`。自主 follower 最坏完整记录内的
  RX capture 为 `567.540/721.836/709.588 µs`，相较上一轮
  `1706.128/2110.744/2073.776 µs` 有明显下降；普通模式完整峰值未一致下降。
  比较见 `timing-compare-r1.json`，这是分次有限观测，不是 WCET 上界。
  四板仍未通过当前 `380 µs` 完整 phase 预算，两个计时窗口均有 RX 进展且
  transport/schedule/profile 错误零增长。剩余 live copy、交接和 owner 工作仍须
  用完整记录继续归因，不能以扫描已移出 Core1 宣布实时通过。
- 收尾：trial 授权 epoch `54`，撤销后 epoch `56`、inactive；四板最终 physical
  armed/runtime enabled/adapter started 均为零。源码复核、成功/失败尝试、原始
  SD、构建与计时证据入口为 `review-r1.json`、`slice-manifest.json`，本轮验收
  结论为 PARTIAL。代码提交 `590b9a0` 已通过当前 staged 源码的 P3 凭证门禁；
  文档独立提交，全部文档门禁和提交后指纹核对见 `commit-proof.json`。
- 状态：`TDMA-FLIGHT-002F` 保持 IN PROGRESS；启动准入与串口失败、正式 RAM、
  完整 Core1 WCET、origin 准备、service blackout、同圈多 owner 及特等逐圈保全
  继续保留各自门禁。registry/C11 不变，长期目标保持 active。

### TDMA-PROGRESS-20260912-020 - 编译节点容量实现与四板复核

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002I`。以下数字均为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260912/tdma-node-capacity/`。按用户顺序先封存
  RX 切片，再实施容量辅助切片；未覆盖前序证据，也未操作另一台设备的单板工作。
- 实现：`PROJECT_NODE_CAPACITY` 默认六，CMake 和头文件校验二至八的编译容量，
  统一 Board Identity、TDMA、VDC、RefMem sync、Calibration path/training 本地
  存储。active profile 仍定义实际在线数；固定 SHORT、RefMem 目录、应用模型表及
  Calibration 持久化 stage 不裁剪。既有 HAOFV/profile 准入和直接 runtime、adapter、
  Calibration 入口均约束本地容量，adapter 同时拒绝非法 reference 编号。
- 兼容性：`CALIBRATION_PATH_CRC_LINK_COUNT` 保留原主机 import CRC 的固定逻辑
  序列，缺少的尾项补零。VDC 本地路径表 CRC 随内部布局计算，不声称该 CRC 跨容量
  相同。真实存储 codec/validator 验证共同拓扑的字节兼容及超容量解码拒绝。
- 软件：容量测试 `17 passed`，覆盖二至八边界、失败不修改已有 profile/编号、
  runtime/stage 拒绝、RefMem source canary/peer 保护、VDC 矩阵及六/八容量的二/四/六
  节点存储互读，并验证六容量拒绝七/八节点持久化拓扑；
  RX、command DMA、TRN03、nonblocking 和 origin admission 回归 `134 passed`。
  adapter、runtime、service、VDC、Calibration path/store 和 RefMem application
  contract 测试通过；八容量历史 profile/runtime/adapter/service/VDC/RefMem sync
  真实编译运行通过。早期 fixture 缺依赖、CRC 上下文及临时运行脚本失败保留在
  `capacity-tests-r1/r2/r3`、`legacy-eight-r1/r2`，后续修正未放宽产品判据。
- 构建：六/八容量 Release A/B/Boot 完整构建及 Flash 链接检查均通过。两组 app
  各 `632` 个翻译单元采用对应容量；六容量 build 为 `20260912081155`，源码指纹
  `36fdfc894e34b866ae10cf6498857c0461b1f35905f7635bd416c6561ad83d16`，
  `1012` files；产物摘要见 `source-checkpoint-r1.json` 与 `ram-compare-r1.json`。
- RAM：两套 app 的主 SRAM 净省均为 `2768 B`，scratch_y 另省 `200 B`；
  `.bss` 为 `468448 → 465680 B`，`link_free_bytes` 为 `2592 → 5360 B`。
  正式要求仍由 `DEFAULT_MIN_FREE_BYTES` 定义，本轮快照为 `49152 B`，六容量仍缺
  `43792 B`；两组正式 RAM 检查均 FAIL。没有放宽 RAM/WCET 或借用 PIO0/DMA7。
- P3：四板 OTA 均确认本轮六容量 build，四链路双组测量通过；凭证 scope 为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coded marker 保留 ARM 拒绝、串口响应形状异常和缺 summary 失败；原始 process-image
  保留启动屏障超时。`matrix-r1.json` 从本轮 TRN00/01/02 原件独立生成，
  `passed=true`、`diagnostic_continue=false`，未移植旧 build 的矩阵或凭证。
- 严格失败：`lifecycle-r1/process-before-r1` 在启动屏障通过后，NO1 观察窗口新增
  一个 transport CRC 错误，其他节点未增长；`diagnostic-lifecycle-r1` 的恢复段再次
  记录 NO1 transport CRC 增长。原始计数、坏帧诊断和有界 STOP 均保留；不能用随后
  恢复或诊断流程抹去这些失败。`crc-diagnostic-r1` 随后保留 NO1 在整个采集期间
  新增 `43` 个 transport CRC 错误；四板 SD 分段均能重建、有限包对通过，但原采集
  总结果仍失败。有限窗口的好包不能覆盖窗口之外的坏帧。
- 候选行恢复：当前矩阵自动选择 row `1`，DATA 为 `[5,4,5,5]`；显式 row `3`
  为 `[5,5,5,5]`，两者均来自本轮矩阵候选，未覆盖原矩阵或借用旧验收。
  `lifecycle-row3-r1` 使用既有 `--offset-row-id 3`，process-before、raw-transition、
  process-restored 全部 `passed/closed_loop_passed/realtime_gate_passed=true`、
  `diagnostic_continue=false`。随后四板采集的 transport/schedule/profile bad 增量
  均为零，SD 原件重建、有限包对身份、mailbox CRC 与 owner 装卸边界审计通过。
  这是显式候选行的有限恢复证据；自动选行与采样余量仍未闭合，不能直接把原失败
  归因于容量实现，也不构成最坏时序、同圈多 owner 或长期零误码证明。
- 自主复核：`trial-row3-r1` 使用有限期 trial `912020`，普通采集目标 `25 s`、
  自主采集目标 `90 s`；自主 persona、四板 SD 采集和原件审计通过，最后撤销授权、
  确认四板 physical armed/runtime enabled/adapter started 均为零。普通完整记录
  峰值依 NO1..NO4 为 `1221.004/987.268/1246.964/1262.896 µs`；自主为
  `877.932/2292.692/2517.876/2541.132 µs`，均未满足当前 `380 µs` phase 预算。
  自主 follower 最坏完整记录中的 RX capture 仍达 `1706.128/2110.744/2073.776 µs`。
  这些是有限观测，不是 WCET 上界；容量收益不能代替 scanner/origin 的后续拆分。
  命令、原始拒绝、软件/构建/OTA/运行证据、候选行对照和主控复核分别收录于
  `row-comparison-r1.json`、`review-r1.json` 与 `slice-manifest.json`，提交证明另见
  `commit-proof.json`；代码、文档按仓库门禁分离提交。
- 状态：`TDMA-FLIGHT-002I` 保持 IN PROGRESS。实现、编译收益及四板候选证据不等于
  六板/混合容量实板通过；严格校准/启动/CRC 失败、完整 Core1 WCET、正式 RAM、
  scanner/origin 异步拆分及特等逐圈通道仍需各自验收。registry/C11 状态不变，
  长期目标 `TDMA-FLIGHT-002` 保持 active。

### TDMA-PROGRESS-20260912-019 - RX 切片封存后的六节点 RAM 隔离核算

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002I`。以下数字均为实验快照，非事实源。
- 顺序：先封存 `TDMA-PROGRESS-20260912-018`，代码提交 `2b7fb79`、文档提交
  `f47751e`，再以干净 HEAD 进行容量核算。该核算没有修改受控固件、运行配置或板端
  状态，没有重用旧 P3 凭证为六节点候选放行。
- 方法：使用前一切片 `build-r2/compile_commands.json` 的 ARM Release 原始参数，
  在证据目录内复制源码，只将 `BOARD_IDENTITY_MAX_NODES`、`TDMA_RING_NODE_MAX`、
  `VDC_DOMAIN_NODE_COUNT`、`REFMEM_SYNC_NODE_COUNT`、`CALIBRATION_PATH_MAX_LINKS`
  和 `CALIBRATION_TRAINING_PHASE_MAX_NODES` 从八改为六。八节点/六节点各重编译
  158 个项目翻译单元，316 次编译全部成功；当前 map 内存活静态对象的八节点尺寸与
  已验收对象逐项相符，未链接的对象和 Flash 常量不计收益。
- 范围：保留 `TDMA_FLIGHT_SHORT_SLOT_COUNT`、SHORT packet/payload 布局、
  `DISTRIBUTED_REFMEM_TABLE_SIZE`、`DISTRIBUTED_REFMEM_NODE_COUNT`、应用模型表，
  以及 `TDMA_RING_CALIBRATION_LINK_MAX` 和 Calibration 持久化 payload 容量。
  仅限制实际在线节点数不会自动释放静态 RAM。RefMem 固定目录不能按节点比例缩减；
  校准 stage/训练缓存进一步裁剪须先解耦运行容量和持久化格式，本次收益不包含该项。
- 静态对象收益：RefMem 三处同步上下文合计 1,728 B、VDC command retention 128 B、
  飞行同步的 per-source 序号 8 B；Calibration 三份 path snapshot 与 import links
  合计 704 B；VDC runtime path table 200 B，其中矩阵 112 B、路径 entry 88 B。
  主 SRAM 合计 **2,768 B**；发布 snapshot 另省 **200 B scratch_y**，不计入主 SRAM
  余量。堆、任务栈预留、DMA ring 和固定 packet 池未缩减。
- 链接复核：两组隔离对象各重链接一次，八节点各段尺寸/地址与验收 ELF 完全相符。
  六节点 `.bss` 从 468,448 B 降为 465,680 B，`link_free_bytes` 从 2,592 B 增至
  **5,360 B**，主 SRAM 净增 **2,768 B（2.703125 KiB）**。两组正式 RAM checker
  均返回 FAIL，要求仍为 49,152 B；六节点候选仍缺 43,792 B。容量缩减不能代替
  完整 Core1 WCET、原始 RX scanner 拆分和特等快速通道验收。
- 原件：`input-r1.json` 保存源码哈希、容量边界和完整编译命令；
  `compile-results-r1.json`、`report-r1.json` 保存逐对象结果；
  `link-report-r1.json` 保存编译器版本、ELF/map 哈希和完整链接指标。
  `eight-baseline/`、`six-local/` 保留测量 ELF/map、链接日志及正式 RAM FAIL 日志。
  这些是容量测量产物，不是可部署六节点固件或硬件验收凭证。
- 后续：`TDMA-FLIGHT-002I` 保持 `PENDING`。独立实现编译容量单一配置、HAOFV
  准入、超容量拓扑/编号拒绝及兼容性验证，再完成软件测试、受影响构建和真实 P3/短帧
  复核；不更改 registry/C11 状态，`TDMA-FLIGHT-002` 继续执行。

### TDMA-PROGRESS-20260912-018 - 普通 RX 的 Core0 异步解析与采集证据绑定

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
- 变更：新增 `tdma_rx_prepare_t` 固定工位，Core1 交入 packet 副本、原始 RX/TX latch、
  RTT、origin observation 与 owner 匹配资格。Core0 完成 transport decode/CRC、
  自主 origin mailbox 完整性检查和参考节点坏帧诊断；Core1 只消费当前 epoch、
  配置/map、时间资格和采集年龄合格的结果，继续独占 health、novelty、FIFO 与生命周期
  事实。结果提交不重读最新硬件时间，也不延长旧帧的接收年龄。
- 所有权：Core0 不持有 adapter、engine、FIFO、DMA 或 origin bank 指针；bank 复用
  不替换已采集帧的 observation 与 owner generation。STOP 先取消资格并停硬件，
  若 worker 已 claim 则保留静态工位直到取消 ACK，旧 READY 不能进入新 ARM。
  忙工位不等待、不再次采集和覆盖输入；这仍是允许缺口的普通观察路径。
- 软件：adapter 主机回归覆盖 Core0 暂停、输入复用、采集时的 latch/RTT 保全、
  REQUESTED/BUILDING/READY 取消、ARM 拒绝、epoch/config/map/age 失效、坏帧诊断、
  异步 bootstrap→origin、成对元数据/bank 复用以及 mutable remote mailbox CRC 拒绝。
  实际 scanner、DMA、非阻塞与短帧工具相关回归 `129 passed`；文档检查器单测
  `18 passed`。首轮测试在启动后绑定 RTT callback，绑定按既有规则被拒，fixture 修正
  后回归通过；失败原件保留，未更改接口生命周期来适配测试。
- 资源：首次 `build-r1` 因 RAM 超出 `296 B` 链接失败，未部署。随后把物理工位与
  原有注入队列做 STOPPED 互斥绑定，拒绝重绑及注入覆盖，不增加 DMA/PIO/任务栈。
  ARM ABI 工位 `840 B`，复用既有 `2496 B` 池，整机静态区相对前版仅增加 `8 B`。
  `build-r2` 的 Release、A/B/Boot 链接门禁通过；正式 RAM 仍以余量 `2592 B`
  小于要求 `49152 B` 判 FAIL，未放宽门槛。build 为 `20260912071055`，源码
  SHA-256 与产物哈希绑定在 `source-checkpoint-r1.json`。
- 实板：四板 OTA 和 P3 quick diagnostic 流程完成并签发当前源码凭证，
  `strict_gates_passed=false`。初始启动屏障在原 `2 s` 窗口内仅获得所需两个稳定
  样本中的一个；首样本含 receive reject/incomplete 增长，失败原件保留。
  同源码同 generation 的实测矩阵通过且 `diagnostic_continue=false`。
  `lifecycle-r1` 首次 process-image ARM 在 NO2 遇到 `arm_result=8` 配置拒绝；
  既有有界恢复 STOP 后第二次严格通过。raw-flight 与恢复 process-image 首次严格
  通过，三段通过记录均无 diagnostic continue。四板原始 SD capture 全部重建，
  有限 CRC、sequence 与 owner 范围审计通过。
- 计时：普通模式与有限许可证自主模式均完成完整 phase 记录，下表单位为微秒。
  RX 分项取各自有限查询记录中有调用的最大值，与完整峰值不一定在同一拍，不能
  相加或视为 WCET 上界。`RX_PARSE` 现在度量 Core1 的结果验证与事实提交。

  | 节点 | 普通完整峰值 | 自主完整峰值 | 自主 RX capture 采样最大值 | 自主 RX_PARSE 采样最大值 |
  |---|---:|---:|---:|---:|
  | NO1 | 1249.580 | 893.948 | 65.664 | 307.660 |
  | NO2 | 1002.432 | 2484.624 | 1954.332 | 208.244 |
  | NO3 | 1238.584 | 2499.692 | 2027.832 | 204.188 |
  | NO4 | 1206.168 | 2508.672 | 2083.900 | 215.324 |

- 对照：相对上一 overlay 切片，普通完整峰值 NO1 下降约 `6.8%`，从站下降约
  `23.8%–26.7%`；自主 NO1 下降约 `26.7%`，从站下降约 `7.5%–16.0%`。自主
  RX_PARSE 的有限分项样本最大值从 `[639.568,487.064,506.780,473.772]` 降为
  表内值，但所有节点完整 phase 仍超出当前 schedule 预算。普通 NO1/NO3/NO4 的
  查询未采到有 RX_PARSE 调用的记录，明确记为未采到，不能记作零耗时。
  从站自主完整峰值仍由原始 DMA 扫描主导，结果提交本身也仍需收敛。
- 基础运输：两种计时窗口的 transport/schedule/profile 错误增量均为零；普通观察
  drop 增量为零，自主为 `[0,31662,27834,27420]`。普通镜像允许缺口，这不证明
  时间戳逐圈无损。`stage-comparison.json` 保留每个分项有调用记录的样本数和范围。
- 自主取证：trial `912018`、grant epoch `76` 下确认自主状态，四板原始 SD 下载
  与有限 owner/CRC 审计首次通过，完成自主计时窗口后撤销到 epoch `78` inactive。
  `final-stopped.json` 证明四板 `armed/ring_enabled/adapter_started` 全为零。
- 证据入口：`out/HardwareAcceptance/20260912/tdma-flight-async-rx/` 中的
  `hardware-review.json`、`slice-manifest.json`、`source-checkpoint-r1.json`、
  `ownership-review.json` 与原始日志；分离提交与门禁核验见 `commit-proof.json`。
  完整 Core1 WCET、正式 RAM、原始 DMA 扫描、origin 准备、service blackout、
  Core0 拥塞、同圈多 owner 和特等快速通道逐圈保全继续作为未闭合项。

### TDMA-PROGRESS-20260912-017 - follower overlay 的 Core0 异步准备

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
- 变更：Core1 复制有效 RX 模型、本地 mailbox 和紧凑固定授权，通过
  `tdma_overlay_prepare_t` 向 Core0 发布请求。现有 Core0 数据任务执行纯编码、校验与
  overlay 构建，不写 adapter/engine 的 Core1 事实或硬件寄存器。完整 READY 发布后，
  Core1 检查 epoch、map generation、alignment 和闲置池，再绑定固定上限的 DMA
  描述符。已就绪 TX 不依赖本拍普通 RX；无新版本保留现有硬件计划。
- 生命周期：输入是独立副本，FIFO 槽复用不影响构建；输出只借用现有闲置计划池，
  DMA 选择后继计划前旧池不可复用。STOP 立即执行硬件停止，取消已发布请求；若
  Core0 仍持有写权限，STOP 保持 pending，迟到 worker 只交还权限，不能进入新 ARM。
  该 worker 的轮询有界，不在 Core1 等待后台计算。
- 软件：adapter 主机负测覆盖无 RX、坏 RX、Core0 暂停、源槽复用、整图生产者到本地
  mailbox 规范化、旧 DMA 池保全、取消前后 ARM、旧 READY 结果和越权 layout 拒绝；
  实际物理 grant/commit 与 DMA 总线模型共同检查旧计划持续运行、epoch/alignment
  失效及选择后退休。相关 Python/实际 C 回归 `129 passed`，adapter host runner 通过。
  首轮缺失 include、fixture 签名未同步的失败原件保留。
- 资源：首次构建因 RAM 链接区超出 `204 B` 失败，未上板。授权快照随后缩为固定本地
  segment 资格，TX 输入只复制 `TDMA_FLIGHT_SHORT_SLOT_SIZE`；仍使用既有计划池，
  不放宽 RAM/WCET 门槛。最终 job 为 `476 B`，链接余量相对前版减少 `492 B`；
  `build-r2` 的 Release 及 A/B/Boot 链接门禁通过，正式 RAM 仍以余量 `2600 B`
  小于要求 `49152 B` 判 FAIL。build 为 `20260912061753`，源码指纹及产物哈希见
  `source-checkpoint-r1.json`。
- 实板：同包四板 OTA 与 P3 quick diagnostic 流程完成；首次短帧启动屏障失败原件
  保留。第一轮观测包含 receive reject/incomplete 增长，后续出现干净样本，但未达到
  原启动期限内所需连续样本数，不能写成严格 P3 通过。使用本源码同 generation
  实测数据生成 `matrix-r1.json`，重装安全行通过且 `diagnostic_continue=false`。
  `lifecycle-r1` 的 process-image → raw-flight → process-image 三段均首次严格通过；
  四板原始 SD capture 可重建，CRC/sequence/owner 范围审计通过，基础运输错误增量为零。
- 计时：普通与有限临时许可证自主模式均采集完整 phase；下表分项来自各节点同一次
  自主完整峰值，单位为微秒，事实源为 `trial-r1/normal-audit.json` 与
  `trial-r1/autonomous-audit.json`，不是 WCET 上界证明。

  | 节点 | 普通完整峰值 | 自主完整峰值 | 自主 RX capture | 自主 RX parse | 自主 overlay |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 1341.272 | 1219.204 | 64.688 | 639.568 | 0.000 |
  | NO2 | 1368.072 | 2685.240 | 1880.912 | 487.064 | 60.320 |
  | NO3 | 1625.776 | 2976.964 | 2107.376 | 394.524 | 104.936 |
  | NO4 | 1596.108 | 2874.376 | 2152.672 | 380.464 | 79.900 |

- 对照：相对 `tdma-flight-rx-budget` 切片，从站普通完整峰值下降约 `39.7%–44.1%`，
  自主下降约 `19.6%–27.1%`；NO1 普通/自主分别下降约 `0.4%/1.1%`。全部节点仍超出
  当前 schedule 的完整预算，RX 捕获/解析仍是主要开销。自主计时区间基础运输错误
  增量为零，普通观察副本 drop 增量为 `[0,12009,20481,21282]`，不证明同步样本无损。
- 自主取证：临时许可 epoch `66` 下确认自主 state，完成计时后撤销到 epoch `68`
  inactive，四板 STOP 成功。四板初始 SD 下载均截断，原 capture/trial 失败保持原样；
  停机后只重读相同冻结文件，未重新 ARM/SAVE。`capture-audit-r2.json` 的原始重建与
  有限 owner/CRC 审计全通过，原窗口基础运输错误增量为零；不据此消除串口下载失败。
- 证据入口：`out/HardwareAcceptance/20260912/tdma-flight-async-overlay/hardware-review.json`
  与 `slice-manifest.json`，各次失败、软件检查、构建、P3、矩阵、短帧、原始波形和
  计时对照均保留引用。普通 RX 捕获/解析、origin 准备、完整 Core1 WCET、无更新
  节拍、service blackout、同圈多 owner 与特等同步时间戳逐圈保全仍未完成。

### TDMA-PROGRESS-20260912-016 - 将异步准备与解析纳入列车调度基础

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/002F`。用户再次明确“检票入站”
  时间不纳入列车环路；因此异步准备/消费属于当前列车基础，服务等级策略仍后置。
- 变更：依据已完成的代码提交 `285ff44` 和完整计时，更新 Architecture 的待实施
  owner 边界及 TODO 进入顺序。`002F` 进入 IN PROGRESS；`002G/002H` 仍 PENDING。
  本记录未实现新的 Core0 后台构建、RX 卸载或跨核发布，也不提升 registry/C11 状态。
- 源码核验：follower 的 `prepare_process_overlay()` 仍位于 `if (rx_ok)` 内；其实现
  在 Core1 解码、复制模型、执行 flight engine 并构图。物理层
  `tdma_pio_spi_phys_prepare_process_overlay()` 还混合了纯计划构建与 DMA 发布。
  既有 flight FIFO 已提供 Core0 payload 发布和业务消费，但 payload READY 不等于
  DMA 可直接使用的完整计划，不能仅改变函数调用所在核。
- 下一 gate：先拆开纯 overlay 构建与硬件提交，明确输入/构建池 lease、停止取消与
  epoch，再接入 Core0 准备和 Core1 有界发布；已准备 TX 不依赖本拍普通 RX 解析成功。
  普通解析在固定记录交接后执行，其结果经版本机制返回唯一 TDMA owner；不得直接
  从 Core0 修改 runtime/flight engine。随后验证 Core0 拥塞、迟到准备和解析停顿时
  环路节拍、旧版本复用、STOP、资源与完整 Core1 WCET。
- 证据：`out/HardwareAcceptance/20260912/tdma-flight-async-platform/owner-boundary-r2.json`。
  本项为源码边界与执行顺序核验，不生成新固件或新的硬件通过声明；基础运输完整性
  和同步时间的独立硬件保全仍按原契约约束。

### TDMA-PROGRESS-20260912-015 - 限制同拍 RX 工作量与 SCK 前置证据恢复

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。以下数字为实验快照，非事实源。
- 变更：以 `0888b0a` 为基线，代码提交 `285ff44`。硬件飞行副本每 phase 只调用一次捕获/解析；软件
  store-forward 保留独立的有界排空。scanner 每个候选只读取共享字节前缀，再在
  寄存器检查各 bit phase；保留 header、CRC、对齐稳定性及复制后 epoch/覆盖复验。
  不增加静态 RAM、PIO/SM/DMA 资源或乘客服务等级。
- 软件与构建：实际 scanner 的两种 ISR 方向、全部 bit phase、SRAM wrap、假 magic、
  部分帧、覆盖和计数歧义回归通过；持续 ready 的 adapter 每 phase 只处理一个副本，
  坏帧仍拒绝，后继正常序列可恢复且无软件重发。相关 Python/实际 C 回归
  `129 passed`，adapter host runner 通过；首次测试 stub 编译错误保留。上一版真实
  scanner 对新 SRAM 读取预算按预期失败，见 `old-scanner-check.json`。
  Release build `20260912050200`，源指纹
  `2dc1b351547ec9130e391ff846c837f6b4967625d51595195ecec28e8d53189b`，源文件数
  `1001`，产物摘要见 `source-checkpoint-r1.json`；RAM 仍为 free `3092 B` / minimum
  `49152 B`，门禁失败，不放宽预算。
- P3 与前置拒绝：`p3-r1` 四板 OTA 成功；顺序复位后 `p3-r2` 使用同包和在线 build
  核验恢复实测。两份真实凭证均仅为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` PASS，
  `strict_gates_passed=false`。粗 CLK ARM8、coded marker、SCK 候选覆盖不足、启动超时
  及 NO2 flight map ARM5 原件保留。`lifecycle-r1/r2` 在主机检查 SCK opposite-edge
  重装余量时拒绝，未进入自主试验。
- SCK 取证：单链路 ARM 回复截断、SD 下载非法 hex、原时限内未收齐整行分别保存。
  `sck-waveform-r2-tail/` 仅收集同一已保存 epoch `177` 的迟到回复，未重新捕获或改写
  原失败。七个存储页完整回复均超过原 `3 s` 期限，范围约 `3.03–3.25 s`；已收字节
  的最大相邻间隔约 `16 ms`，本记录不能证明 idle-gap 提前结束是根因。
  `sck-recovery-audit.json` 独立验证头部/CRC/训练身份，并恢复 residual `0`；该原件
  缺完整板端状态元组，未被补入合格矩阵。
- 校准恢复：持久连接补测 `sck-matrix-r3` 仍因 link0 候选覆盖失败；使用已有
  `--short-open` 的有界对照 `sck-short-open-r4` 共 `13` 次通过，三个 follower 都取得
  `0/1` 候选。与同源码、同 Calibration generation 的 DATA/residence 原件生成
  `matrix-r4.json`，`passed=true`、`diagnostic_continue=false`，选中 `[1,0,0,0]`，
  最小重装余量 `0` sample。未使用旧源码矩阵、手改候选或放宽重装预算；对照结果
  尚不足以证明持久连接导致采样偏置。
- 短帧闭环：`lifecycle-r3` 的 process-image → raw-flight → 恢复 process-image 最终
  均为 `passed/closed_loop_passed/realtime_gate_passed=true`、`diagnostic_continue=false`。
  raw-flight 首次 NO2 ARM8 拒绝，经 STOP 后下一次通过，原失败保留。四板 SD 原始
  segment 重建、CRC/sequence/owner 范围审计通过，采集窗口基础 transport 错误增量
  为零，最终 STOP 通过。证据见 `lifecycle-r3/validation.json`、`capture-audit.json`。
- 完整计时：`prepare-r1` 显式诊断配置通过；`trial-r1` 取得临时许可 epoch `76`，
  实读自主状态 `5`，普通模式取样后，在自主原始采集结束后继续取样。下表的每个
  peak 及其子项均属于某一次完整 phase；前后是同板、同 persona、同 clk_sys 和预算
  的独立有限窗口，不能把峰值降幅当作已证明的 WCET 上界。

  | 节点 | 普通完整 peak µs | 自主旧 peak µs | 自主新 peak µs | 自主新 RX capture µs / calls | 自主新 RX parse µs | 自主新 overlay µs |
  |---|---|---|---|---|---|---|
  | NO1 | 1346.896 | 1185.548 | 1233.220 | 43.392 / 1 | 545.972 | 0 |
  | NO2 | 2409.916 | 8537.728 | 3617.076 | 1944.840 / 1 | 192.600 | 1267.876 |
  | NO3 | 2696.252 | 9580.068 | 3702.320 | 1654.108 / 1 | 413.008 | 1261.336 |
  | NO4 | 2857.184 | 9691.712 | 3945.428 | 1932.944 / 1 | 468.568 | 1353.172 |

- 归因与限制：自主从站有限 peak 下降约 `57.6–61.4%`；NO1 增加约 `4.0%`，不宣称
  全节点一致加速。全部采样的 RX capture calls 不超过一次，完整 `SCHEDULE` 预算
  仍为 `380 µs`，全部节点仍失败；RX capture 和 overlay 准备仍是从站主要开销。
  自主计时窗口基础 transport 错误增量均为零，可丢弃观察副本的 drop 增量依次为
  `0/31921/35380/35278`，不代表逐圈记录无损。原始数据与逐节点 overrun、计数和
  对比范围见 `trial-r1/normal-audit.json`、`autonomous-audit.json` 及两份 comparison。
- 自主采集与停止：NO2–NO4 原始捕获通过；NO1 首次下载声明 `512 B`、实际 `487 B`，
  原 capture 和首次 audit 失败保留。撤销许可并确认四板 STOP 后，仅重读同一冻结
  捕获，原始 segment/CRC/sequence/owner 复核通过，见
  `trial-r1/recovered/capture1-node0/recovery.json`、`capture-audit-r2.json`。
  该恢复不改变原 trial 的非零退出码。许可撤销后 inactive，四板
  `armed/enabled/adapter_started=0`，无本轮 cleanup error。
- 证据已由 `hardware-review.json` / `slice-manifest.json` 封存。下一 gate 继续收敛
  RX capture、解析与 overlay 准备，按 owner/版本/静态资源边界推进异步准备与消费；
  完整 Core1 WCET、无更新稳态及真正 service blackout 均保持开放。
  不由局部工作量限制推导逐圈同步样本或列车调度完成；
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，`002F/002G/002H` 保持 PENDING，registry/C11 不变。

### TDMA-PROGRESS-20260912-014 - 列车调度分项计时与重复 RX 开销归因

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。以下数字为实验快照，非事实源。
- 变更：以 `4d8a715` 为基线，代码提交 `6284fb0`。为已有 Core1 TDMA phase 加入固定记录；使用 clk_sys
  TIMER1 原始拍数，保全最近完整 phase 与某一次最慢完整 phase 的嵌套步骤。Core0
  单次尝试读取，reset 请求由 Core1 下次 phase 消费。完整 `SCHEDULE` 仍包含计时
  开销；本切片没有改变发车、续转、CRC、所有权、PIO/DMA 资源或乘客服务策略。
- 软件与构建：计时器实际 C 回归及 nonblocking/NO5/analyzer/TRN03 相关测试
  `133 passed`；实际 C adapter host runner 通过。Release build `20260912041214`，
  源指纹 `a59eb65d3a99ffe43d256b6b371493285f9d88a3cb387f6c5b6adfe2aeaef764`，
  源文件数 `1001`；构建产物和 SHA-256 见 `source-checkpoint-r1.json`。
- P3：首次 NO4 OTA 失败，原件见 `p3-r1/` 与 `ota-r1-audit.json`；同包第二轮四板
  OTA 成功，`forced_continue=false`。`p3-receipt-r2.json` 仅为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` PASS，`strict_gates_passed=false`；CLK、coded marker
  和 NO3 `arm_result=5` 均保留失败。独立 `lifecycle-r1` 再现 NO3 flight map 配置拒绝，
  尚未进入物理 ARM；停止态 configured/active 快照不足以确定 validate/lock/active
  中哪个分支拒绝。受控复位恢复后，`lifecycle-r2` 的 process-image、raw-flight、恢复
  process-image 三段 `passed/closed_loop_passed/realtime_gate_passed=true`，均未诊断
  继续；四板原始 SD 重建、CRC/sequence/owner 改写范围复核通过，最终 STOP 通过。
- 计时结果：普通严格配置 `trial-r1/normal-audit.json` 的各板最慢完整记录见下表。
  自主诊断配置 `trial-r2/autonomous-audit.json` 使用新的 reset epoch，在原始采集结束
  后持续取样；表中子项全部来自对应那一次最慢完整 phase，不拼接独立峰值。

  | 节点 | 普通完整记录 µs | 自主完整记录 µs | 自主 RX capture µs / calls | 自主 RX parse µs / calls | 自主 overlay prepare µs |
  |---|---|---|---|---|---|
  | NO1 | 1439.972 | 1185.548 | 43.836 / 1 | 507.684 / 1 | 0 |
  | NO2 | 2622.348 | 8537.728 | 5524.812 / 4 | 1499.672 / 4 | 1185.160 |
  | NO3 | 2962.880 | 9580.068 | 6928.504 / 4 | 1341.600 / 4 | 1055.320 |
  | NO4 | 3015.644 | 9691.712 | 7096.264 / 4 | 1212.648 / 4 | 1101.032 |

- 归因边界：自主从站最慢记录主要消耗在 `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 的重复
  捕获/扫描，其次是多次解析和本地 overlay 准备。父子计时包含式，不能相加；有限
  peak 不是已证明的 WCET 上界，也不能和另一时刻的 `SCHEDULE` 当作同一次原子采样。
  完整 phase 预算仍按实际 `SCHEDULE`，本轮为 `380 µs`，全部节点未通过。
- 临时许可与采集：首次 prepare 未启用诊断配置，许可命令被拒绝，见
  `trial-r1/grant-rejection.json`；保留原始 `<timeout>` 和执行错误，没有更改门禁。
  `prepare-r2` 显式诊断配置通过后，`trial-r2` 取得 epoch `58`，实读自主状态 `5`。
  NO1 首次 SD 下载出现 declared `512 B` / actual `504 B`，其余三板采集和原始 owner
  复核通过。停机后仅重读同一已导出捕获，原始失败与恢复审计分别留档，不能提升原
  capture 的 `passed`。自主计时窗口内基础 transport 错误增量见各板原始 audit。
- 失败/恢复：许可证已撤销。NO4 首次 STOP 返回超时，但同时刻最终快照四板均为
  `armed/enabled/adapter_started=0`；`stop-confirmation-r1` 再次取得四板 OK 与停止
  快照。复位曾有并行 USB 枚举失败，随后顺序重试成功，全部原日志保留。
  RAM gate 仍失败：free `3092 B`，minimum `49152 B`；不放宽阈值。
- 证据根：`out/HardwareAcceptance/20260912/tdma-flight-service-timing/`；源码、命令、
  P3、前置闭环、原始捕获、计时和失败通过 `hardware-review.json` / `slice-manifest.json`
  关联，官方 P3 凭证另由 staged gate 核验。主控复核不改变 registry/C11 状态。
- 下一 gate：先压缩单 phase 重复 RX 工作并优化有界扫描，再拆分仍超预算的解析/
  overlay 准备；每个实现切片重新 build/P3/短帧/原始波形和完整计时。随后完成真正
  service blackout、无更新稳态和 STOP 生命周期。`TDMA-FLIGHT-002B` 保持 IN PROGRESS，
  `002F/002G/002H` 保持 PENDING；本轮不宣称列车调度或乘客逐圈服务已完成。

### TDMA-PROGRESS-20260912-013 - 异步候车平台与四级服务设计核验

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/002C/002F/002G/002H`，归属 `TDMA-FLIGHT-002`。
- 变更/提交：按用户要求先完成在制代码提交 `7bfbcdef6efb3d5869caf358986b39e58139b326`
  与文档提交 `252ea7bdaff8ab45629b6c06e83bbcdf40415499`。提交后工作区干净；真实源码
  指纹与本轮 P3 相符，代码 commit 通过非空暂存区硬件门禁。新建的
  `tdma-flight-rx-observation/commit-proof.json` 绑定两个提交、实际 hook 日志和提交态
  文档 hash；原 sealed manifest 的提交前文档快照保留。没有 push。
- 本次范围：读取现有代码与 HAOFV/状态机/VDC 契约，维护 TDMA 三件套中的待实施设计和
  退出门禁。未修改固件、PIO、工具或测试实现，未烧录或启动板卡；不引用历史 P3 作为
  新实现验收，也未改变 registry/C11 状态。
- 源码核验结果：
  - `tdma_flight_fifo_core0_publish_tx()` 已能发布固定池的完整版本，物理双计划也已有
    pending/selected 生命周期；follower 的 `prepare_process_overlay()` 仍受本次
    `rx_ok` 门控，违背方向独立推进的目标。
  - `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 会在每次 service 内重复运行 RX 观察与解析；
    单次 scanner 上界不等于整个 TDMA phase 的工作量上界。本次未进行逐 action 实测，
    不能把前轮超预算全部归因于该循环。
  - `tdma_process_image_layout.h` 已固定 timestamp trailer、VDC、RefMem、ACK/control
    和 optional diagnostic 区域；这只提供布局基础，尚不保证独立准备或逐圈卸载。
    当前 composite mailbox 由 `distributed_refmem_tdma_flight_sync_publish()` 统一
    生成，后续要隔离各域准备并保留唯一装配/CRC writer。
  - 自主交接的 `tdma_pio_spi_ring_origin.inc` 清除原正式 timestamp evidence，
    `tdma_pio_spi_phys_origin.inc` 的 RX observation 返回无正式时间戳；因此既有
    自主 persona 证据不能证明特等席每圈有效同步样本。现有协议使用
    `TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG`，不能把前圈样本冒充本圈 latch。
  - 普通 RX mirror 允许拥塞丢弃，不能用作特等席的无损接收队列；必须核算独立记录
    的最坏停顿容量、持续消费能力和资源准入，并保留 overflow/invalid 缺口。
  - `tdma_traffic_scheduler.c` 已区分使用控制封装的有时限 VDC 跟随命令；服务等级应
    按业务含义映射。RefMem ACK/fence、STOP 等生命周期入口也不能跟随普通日志降级。
- 方案结果：明确进站准备、完整 READY、硬件边界 LOAD/UNLOAD 与后台消费；特等逐圈
  同步样本、一等 VDC 跟随数据、二等 RefMem、无座普通控制与 Log 分别给出保留位置、
  版本、迟到/拥塞策略和证据要求。短帧微量 Log 需先定义静态预算和分片契约；既有
  LONG/maintenance 日志能力不能直接视作每圈短帧能力。架构正文记录设计边界，不冻结
  新 contract；整体目标及正式 Core1 WCET、RAM 和产品准入仍未通过。
- 执行顺序调整：用户随后明确“先解决列车调度，再解决乘客调度”。本次四级设计留档，
  `TDMA-FLIGHT-002F/002G/002H` 均为后续 `PENDING`；当前回到自主续转、无更新稳态、
  完整 Core1 WCET 和 STOP 边界。基础运输校验继续执行，特等正式同步能力不冒充已实现。
- 验证与证据：
  - 只读核验：`out/HardwareAcceptance/20260912/tdma-flight-async-platform/design-audit.json`，
    含源码 hash、真实符号/行号、基线提交、解释与下一 gate。
  - 文档门禁与提交凭据：同目录 `docs-*.log`、`doc-tests-r1.log`、`pre-commit-r1.log`、
    `p3-staged-r1.log`、`design-review.json`；最终结论以该复核文件和原始日志为准。
  - 项目文档门禁通过 Git Bash 执行；UTF-8 源码读取和只读指纹记录使用 PowerShell/
    原生 Python。按用户当前日期要求，所有新产物保存在 `20260912`，不改写旧目录。
- 下一 gate：先取得完整 owner phase 与各步骤的 clk_sys 归因，收敛重复轮询、重准备及
  软件发车依赖，完成正反例、fresh build/P3、短帧闭环、无更新自主循环与原始波形。
  列车调度及完整 Core1 时间门禁通过后，再推进异步乘客更新、特等硬件时间样本和
  四级隔离；不提前实现乘客优先级或提升产品同步、周期级飞行声明。

### TDMA-PROGRESS-20260912-012 - RX 观察副本生命周期、计数回归与有界扫描

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。本节数字为实验快照，非事实源。
  证据均位于本文开头的 RX 观察切片目录；前序原始失败保留。取证时为未提交工作树，
  当前实现已提交为 `7bfbcde`；提交时 pre-commit 核验当前源码的真实 P3 诊断凭证通过，
  不代表严格 P3、正式 RAM 或完整 Core1 WCET 通过。文档与实现分离提交。
- 旧 scanner 在 DMA 覆盖期间可发布混合副本；`scanner-before-r1.log` 的覆盖、完整计数
  周期歧义和扫描工作量反例保留。RX ring 只供可丢弃解析，不能用其重新定位已发布的
  wire slot。当前 scanner 限制每次候选数，完整复制后复验写入边界及 observation epoch。
- 初版 build `20260912025324` 四板 OTA 成功，但 `p3-r1/p0t-topology/summary.json`
  的拓扑失败：无真实 DMA 写入的板卡因软件坐标跳变虚增 16,777,216 字，形成错误的
  全连接邻接图。`counter-r1-source/` 保存该实现；`counter-before-r2.log` 用真实生产
  C scanner 复现长空闲间隔无数据仍增加公开计数。该失败不能归因于线路或另端单板合入。
- 修复将公开接收累计值与 observation epoch 分离。累计值只增加硬件余量的模差，
  歧义之后为下界；失效标记单独拒绝跨缺口副本。DMA 自重装周期由实际物理帧长度投影，
  同时保持 ring 和帧相位，仍由 TDMA owner 在 ARM 配置，不新增资源或逐帧续装动作。
  未锁定 alignment 在计数失连续时重新收集稳定样本；已发布 alignment 保持固定。
- `scanner-after-r4.log` 的 133 项相关 Python/C 回归通过，`sequence-r3.log` 的实际
  C counter/旧 sequence 单测通过；覆盖无数据长间隔、复制中覆盖、同余计数跨 epoch、
  硬件重装零边界、丢整周期后的帧相位及有界扫描。`scanner-after-r3.log` 的旧字符串
  fixture 断言失败保留，更新硬件装载绑定后通过。
- 当前 build-r2 `20260912031142`，源码指纹
  `0ff0c8b7d6309d98a8a6ec3c6b173e6b6c31741d6cec3055d209dcdc0b345bae`，997 文件；
  A/B/Boot、package 和 Flash 链接检查通过。`ram-r2.log` 余量 3,452 B，低于 49,152 B，
  正式 RAM 仍 FAIL。`p3-receipt-r2.json` 为 QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false；coded marker 失败、短帧 ARM result=8 与未交接运行环保留。
  `topology-audit-r2.json` 核验四条实际连接和八组未连接方向，后者接收字节增量均为零。
- `lifecycle-r1` 使用本轮 P3 的新矩阵，process-before、raw-transition、process-restored
  三阶段首次尝试均 passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed=true，
  diagnostic_continue=false。集成采集因 NO3 StorageAO FILE_WRITE error=6 失败；其余
  三板原始采集通过且 transport 错误增量为零。NO3 保留原 capture tag/sequence，第一次
  SAVE 重试仍失败、第二次成功，`recovered/capture1-node2/recovery.json` 留下原始响应。
  `raw-audit.json` 核验四板共 32 段 SHA-256 与完整重建、有限帧配对、owner 改写边界和
  mailbox CRC；恢复原件不把原集成采集改写为通过。
- `baseline-schedule-r1.log` 的区间跨 STOP，adapter 计数已被清零，负 RX delta 不能解释
  为 transport 退步或连续性证据；NO3 失败采集没有运行中的 after snapshot，不补造该
  增量。scheduler 仍记录持续超预算，完整 Core1 WCET 不通过。
  `prepare-trial-r1` 已完成显式诊断短帧启动，不替代前述正常模式三阶段闭环。
- `trial-r1` 使用 360 s 有限许可证（trial=912012、epoch=66），实际进入 AUTONOMOUS、
  persona16；下载完成后继续观察至少 90 s。四板原始采集首次均通过；`raw-audit.json`
  核验另外 32 段及有限 owner/CRC 配对。`runtime-audit-r2.json` 的 14 组自主样本显示
  四板接收持续推进且 transport 错误增量均为零，三个 follower 均在配置/persona/armed
  保持不变时出现 self-trigger DMA count 重装。origin 使用不同计数模式，其普通 count
  增加不作自重装证据；首版 runtime 审计未区分模式的索引字段不用于该结论。
- 同区间 NO2/NO3/NO4 软件观察丢弃分别增加 432/27,449/28,460，拆拍扫描分别增加
  10/118/84；这是解析副本丢弃，不能改写为线路错误，也不能据此免除 Core1 预算。
  NO1 TDMA 最近样本范围 179.292–692.148 µs；NO2/NO3/NO4 样本最高分别
  3,528.620/6,657.104/4,489.756 µs，均有当前区间 overrun 增长，超过 380 µs 预算。
  完整 Core1 WCET 明确 FAIL，不能只报告自主线路连续。`CALibration:ORIGin:REVOKe`
  成功后发布 epoch=68、enabled=0；`final-stop/final-stopped.json` 确认四板停止。
- 下一切片优先核对 `tdma_pio_spi_ring_adapter_rx_poll()` 中
  `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 对 scanner 上界的乘算及整段解析/overlay 成本，
  在 owner service 边界收紧工作量；目前没有各内部步骤的实板耗时归因，不能把全部超时
  归因于 scanner。尚未注入 service blackout，未证明同圈多 owner 更新、最坏仲裁、
  故障恢复或长稳；历史自主 trial 的间歇 transport 错误根因也没有被本次有限窗口证明。
  registry/C11 状态不变，正式 RAM、严格 P3 与产品准入保持未通过。复核入口为当前
  切片的 `hardware-review.json` 和 `slice-manifest.json`。

### TDMA-PROGRESS-20260912-011 - 临时许可证接入、实际自主运行与失效闭环取证

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树。以下数字均为实验快照，非事实源；新证据根及旧来源见本文开头，复核入口为
  `hardware-review.json`。用户授权“调试阶段发临时许可证”，产品门禁没有豁免。
- Calibration 发布有限期、版本化的 `calibration_origin_timing_t`，绑定配置、校准/拓扑、
  model epoch、foundation/DeploymentGate、board/persona/resource 与时钟。唯一 TDMA
  owner 在已接受返回帧、旧 TX 完成且下一次 TX 决策前消费；每个准备步骤复验，FAULT、
  失效及到期通过 STOP 清理。已消费 epoch 跨 STOP 保留，清理失败不自动恢复旧发车器。
  SCPI 入口为 `CALibration:ORIGin:TRIAL`、`CALibration:ORIGin:REVOKe` 及两个只读查询。
- 软件：旧证据根 `admission-r3.log` 的 125 项 Python 回归通过；实际 C adapter、model、
  scheduler、RefMem、VDC 回归通过。覆盖 odd/stale/revoked/expired、配置/模型/资源变化、
  准备失效、FAULT STOP、清理失败与同 epoch 禁止重启。`admission-r1/r2.log` 的 fixture
  编译失败保留。最初全局 snapshot 一次尝试导致旧 SCPI 竞争超时，已拆分新 `try_get`
  有界接口与既有 Core0 重试接口，当前 P3 周期采样无查询错误；不能声称所有读取均有界。
- 当前 build-r2 为 `20260912015104`，源码指纹
  `23969c8f87eea46bc678fdc7d7dd03c5953ecd7dd49bdfde876c8882713da8cd`，995 文件；
  A/B/Boot、package 与 Flash 链接检查通过。`ram-r2.log` 余量 3,484 B，低于 49,152 B，
  正式 RAM FAIL。当前 P3 四板 OTA 通过，凭证为 QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false；粗 CLK ARM result=8、无安全 SCK 候选、启动超时均保留。
  前序 build-r1 已被当前 build 取代，不以其验收替代当前源码。
- 前置恢复：旧证据根 `sck-r4` 的 15 次原始采样形成 `matrix-r4.json`；该输入测于
  build-r1，当前 build 独立完成 `lifecycle-r3` 的 process-before、raw-transition-r2、
  process-restored-r2。三阶段 passed/closed_loop_passed/realtime_gate_passed/
  diagnostic_passed=true，diagnostic_continue=false；32 个 SD 段重建和 owner/CRC
  审计通过。早期 SCK 复用运行身份、参数范围拒绝、unsafe matrix、启动观察期限不足及
  ARM resume 竞争记录保留。`baseline-review.json` 保留该来源区别与完整 WCET FAIL。
- 首次自主试验：旧证据根 `trial-r1` 以显式调试参数 rearm=8192 ticks、abort=256 polls、
  duration=90 s 发布 epoch=96；这些参数不是实测产品授权值。实板进入 AUTONOMOUS、
  persona16、armed=1，返回 sequence 和完整 owner generation 持续推进；诊断准入有效，
  product_valid=0、reject_mask=7。`run_trial.py` exit=0 只表示观测到 persona，不代表
  capture 通过。NO1/NO2 下载截短，NO3/NO4 transport bad 分别增加 2/1，整次 capture FAIL。
- SD 原件恢复后，新证据根 `raw-audit-r1.json` 核验四板共 32 个分段 hash/完整重建，
  有限窗口内配对、节点改写边界与活动 mailbox CRC 通过；零相位采样也能分别解码完整
  CRC 有效包。旧 owner 审计因 integration FAIL 在分析前中止，原错误保留。
  成功窗口不能排除间歇线路错误，也不能消除原始 transport 错误增长或证明同圈多节点更新。
- 许可证失效实板证据：`expiry-r1` 发布 epoch=112，12 s 后自动停止；首次读到停止态距
  主机请求起点 12.078 s。`revoke-r1` 经正常 ARM 后发布新 epoch=128，再次进入自主态，
  主动撤销后 0.156 s 读到停止态。两次均继续观察至少 3 s 未重启，最终四板均为当前
  build、enabled/started/armed=0、config/applied 一致，origin DMA 无忙状态。以上包含
  串口查询延迟，不能当作固件 STOP WCET。到期保留 Calibration 发布记录，运行时已停止；
  主动撤销则更新 epoch 并清 enabled。同 epoch 消费负测仍以 host 为证，不能由新 epoch
  重入试验代替。两次前置短帧闭环通过，但启用了 diagnostic_continue。
- `scanner-probe-r1/report.json` 执行未修改的生产 scanner：DMA 在复制期间推进并覆盖
  16 个尚未读取字节，scanner 仍发布混合副本，且只读取一次 produced。该反例证明活动
  RX ring 复制存在竞态；没有证明它就是实板 transport 错误的根因。修复需同时约束扫描
  工作量、检测副本覆盖并保持 wire slot 所有权；单纯 modulo pointer 不足以证明任意
  停顿后的完整生命周期。本轮没有据此修改 scanner 固件或屏蔽 transport 错误。
- 完整 Core1 WCET 仍 FAIL：首次自主试验的 NO1 TDMA 最近执行样本约 398–562 µs，
  超过该配置 380 µs 预算，overrun 持续增长；历史最大值不能归因到某一个函数。
  下一 gate 为 RX 副本竞态/transport 错误定位与完整 WCET 收敛，然后执行真正 service
  blackout、同圈更新、拥塞和故障/长稳验收。正式 RAM、严格校准和 C11 仍开放，
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，registry 状态未提升，未暂存或提交。

### TDMA-PROGRESS-20260912-010 - owner intent 有界读取与窗口等待拆拍

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树；证据根 `out/HardwareAcceptance/20260911/tdma-flight-service-boundary/`。
  以下数字为实验快照，非事实源；前序证据由 origin status manifest 保留。
- 复核自主发车准入接口时，确认当前 `tdma_service_core1_service()` 的 intent 读取
  无限重试与计划窗口忙等。改为每拍一次完整读取，sequence/abort/payload 在同一
  guard 下验证后才发布 ARM 或 completion；读到写入态或版本变化则返回。常驻环路
  service 和 STOP 仍先推进。窗口未到时保留命令并发布 WAITING_FOR_WINDOW；后续拍
  已错过窗口则记录 WINDOW_MISSED，不靠忙等等待边沿，也不在过期后调用 adapter。
- `before-r1.log` 在实际旧 service 上复现五项失败：写入中断不返回、复制期间版本
  变化后错误完成、冻结时钟下的窗口等待。`pytest-r1.log` 119 项通过，最终新增
  resident/STOP 用例由 `pytest-r3.log` 验证，共六项针对性用例、120 项不重复 Python
  用例通过。`pytest-r2.log` 保留测试先 START 后 ARM 的前置条件失败，fixture 已按
  独立 service 边界应用 ARM 后修正。实际 C scheduler、VDC、RefMem 回归均通过。
- build-r1 `20260912005540` 的 A/B/Boot、package 与 Flash 链接检查通过；源码指纹为
  `18797259e0b0585939ee0537b9c73a7e3a266324be409398baf93905a7c1f019`，990 文件。
  `ram-r1.log` 余量仍为 3,900 B，低于 49,152 B，正式 RAM FAIL，无预算豁免。
- `p3-r1/` 四板 OTA 全部通过、无 forced continue；coded marker 校准通过。P3 凭证
  仍为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC、passed=true、strict_gates_passed=false：
  粗 CLK 阶段 NO1 ARM result=5 对应 FLIGHT_MAP_REJECTED；TDMA startup barrier 超时。
  首个样本各板 receive_rejected/rx_bitmap_incomplete 计数增长，随后只有一个稳定
  样本，未达到所需样本数。拒绝分类与样本事实不能当作硬件根因，原始失败均保留。
- `lifecycle-r1/` 的 process/raw/clock-restore 三阶段 passed/closed_loop_passed/
  realtime_gate_passed/diagnostic_passed 均为 true，diagnostic_continue=false。
  32 个原始 SD 段的 hash 与完整重建一致；包配对、follower owner 边界及活动
  mailbox CRC 通过。RX 增量 20,928/20,931/20,939/20,935，transport 错误无增长。
- `hardware-review.json` 保留完整 WCET FAIL：TDMA phase 历史最大值为
  2,711.088/2,599.416/3,194.852/2,916.120 µs，预算 380 µs；本窗口 overrun
  增加 45,325/24,874/30,530/27,042。最大值包含启动/P3，不能归因到本次修改的
  函数。final-stopped 核对四板当前 build、enabled/started/armed=0、config/applied
  一致；origin 历史 TX_BUSY=5 保留，follower 为零。
- 下一 gate 仍是 Calibration 版本化重装窗口授权、现有 capability/DeploymentGate
  投影及下一次旧 TX 决策前的真实 owner 交接。当前正常 ARM 仍使用旧 origin，尚无
  persona16、service blackout 或同圈更新证据。有界读取不替代多 writer 仲裁，其他
  result-frame 读取、raw scanner、adapter action 和兼容窗口时间表示仍须审查；
  不能把本切片写为完整 HAOFV/WCET 通过。不提交未启用的独立 builder，不提升契约状态。

### TDMA-PROGRESS-20260912-009 - 修复准备态累计事实回退

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树；证据根 `out/HardwareAcceptance/20260911/tdma-flight-origin-status/`。
  以下数字为实验快照，非事实源；前一指纹的成功与失败保留在 prepare manifest。
- `tdma_pio_spi_ring_adapter_publish_status()` 复用原有累计事实序列化，在
  PREPARING 提前返回前也执行，保留收发总数和最近序号；running 仍为零，旧绝对
  时间仍失效，不增加 RX/FIFO 操作，不嵌套或跨拍持有 snapshot writer guard。
  新回归先要求 bootstrap 确有非零累计事实，再验证 BUSY、DONE 和 poll 失败时
  对外事实不回退、时间资格仍无效；原始失败由 `counter-r1.log` 保留。
- `adapter-r1.log` 的实际 C adapter/engine/FIFO/FSM 回归通过；`pytest-r1.log`
  相关 114 项通过。build-r1 `20260912002825` 的 A/B/Boot、package 与 Flash 链接
  检查通过；源码指纹为
  `1d30d09afc70c0d667765817dcdd97d2cc1522ad3579211b630e7b5ea36583bb`。
  `ram-r1.log` 余量仍为 3,900 B，低于 49,152 B，正式 RAM FAIL。
- `p3-r1/` 四板 OTA 均通过、无 forced continue；TDMA process-image/FIFO 流程
  通过。凭证仍为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false：粗 CLK 阶段 NO2 ARM result=8 与 coded marker gate
  失败均保留，不能升级为产品或完整校准验收。
- `lifecycle-r1/` process/raw/clock-restore 的 passed/closed_loop_passed/
  realtime_gate_passed/diagnostic_passed 均为 true，diagnostic_continue=false。
  32 个原始 SD 段的 hash 与完整重建一致；包配对、follower owner 边界和活动
  mailbox CRC 全部通过。RX 增量 18,642/18,636/18,646/18,643，transport 错误
  无增长；局部独立窗口不证明全局同一圈更新。
- `hardware-review.json` 保留完整 WCET FAIL：TDMA phase 历史最大值为
  2,809.148/2,615.200/2,958.868/2,960.172 µs，预算 380 µs；本窗口 overrun
  增加 39,467/21,518/26,662/23,556。最大值包含启动/P3，不能单独归因到采集。
  final-stopped 验证四板当前 build、enabled/started/armed=0、config/applied
  一致；origin 的历史 TX_BUSY=5 保留，其他节点为零。
- 下一 gate：接入 Calibration 重装窗口与 profile/epoch 授权、已有 capability/
  DeploymentGate 的准入投影和真实 owner 调用；实际调用点必须在下一次旧 TX
  决策之前。`core1-review.txt` 另列 raw scanner 积压扫描与 intent seqlock 无界
  重试，不能根据 phase 最大值直接归因，后续仍须逐 action 的 clk_sys 实测。
  正常 ARM 仍使用旧 origin；host 准备态修复不等于硅上已进入该状态，也不证明
  自主循环、service blackout、拥塞或同圈更新。`TDMA-FLIGHT-002B` 保持 IN PROGRESS，
  不提交未启用的独立 builder，完整 WCET、RAM、硬件 mailbox 授权及绝对 VDC 时间开放。

### TDMA-PROGRESS-20260912-008 - origin 分步准备候选、当前源码回归与状态发布缺陷

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树，证据根为 `out/HardwareAcceptance/20260911/tdma-flight-origin-prepare/`。
  以下测试、build、时序和内存数字均为实验快照，非事实源；不改变 registry 状态。
- `tdma_origin_plan_begin/step/cancel()` 将两遍构图拆为固定块，BUSY 不发布入口，
  cancel/失败清除可安装入口。离线同步 wrapper 保留，物理准备改用分步接口。
  `builder-report.json` 对 1,016 个活动 mask/位置组合与独立保存的旧 builder 逐字节
  比较，执行图、常量和入口一致；每次构图 86 步，单步最多 23 个描述符，87 个取消
  边界通过。工作量有界不是 clk_sys WCET 证明。
- physical begin/poll 冻结配置和 seed；mailbox 逐个校验，完整 DMA STOP、persona、
  构图、seed、禁用态 SM 配置和安装分步推进。每步复核配置/时钟，安装前复核资源；
  失败保留清理责任。adapter 增加 `RESIDENT_PREPARING`，暂停旧发车和 FIFO 消费，
  DONE 当次不再收割。common STOP 同时覆盖不存在 loader/executor 的旧 capture/output，
  disarm 不再追加无界 abort。`owner-r3.log` 用实际 C preparation/disarm 和 mocked
  MMIO 验证 97 个 poll 边界的 STOP、失败清理保留与成功取消。
- `adapter-r1.log`、`comm-fsm-r1.log` 和 `pytest-r3.log` 通过，后者 143 项。
  `layout-report.json` 由当前 ARM 编译参数及 ELF 交叉核对：workspace 7,696 B、origin
  7,480 B；548 B builder 复用 940 B 未发布字段，新增 physical prepare state 为 96 B。
  build-r1 `20260912000246` 构建/Flash 链接通过，源码指纹为
  `b13f76a831fd0db122140482c829d303083db787d3fea5806b7ab65e59ac78ce`。
  `ram-r1.log` 正式 RAM FAIL：剩余 3,900 B，门禁 49,152 B。
- `p3-r1/` 的 NO2 OTA 明确返回 VERSION_REJECTED、仍运行旧 build；其他板升级成功。
  `no2-ota-repro-r1/` 对同一 package/参数做一次单板重现后升级成功、无 forced continue。
  `ota-rejection-review.json` 核对 package 最低 bootloader 与源码常量一致，但没有失败
  当时接收 header/内存约束的证据，根因仍未解释，禁止用重试成功覆盖原始拒绝。
- `p3-r2/` 重新执行四板真实 OTA，均通过、无 forced continue；P3 凭证为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，passed=true、strict_gates_passed=false。
  粗 CLK topology readback mismatch、coded marker gate 失败、TDMA startup barrier
  超时均保留。startup 在首次样本记录活动 mailbox 不完整/拒绝增长，后续只有一个
  稳定样本；不能把诊断流程退出成功写为严格环路通过。
- `lifecycle-r1/` 独立短帧检查在 NO4 ARM 以 RUNTIME_CONFIG_REJECTED 拒绝。
  原始 physical/runtime/FIFO 快照保留，随后四板 STOP 的 enabled/started/armed 为零、
  config/applied 一致；这些快照不能把拒绝归因到 PIO。后续同矩阵有界重现与原始
  波形复核以 `hardware-review.json` 为准，不覆盖此前失败。
- `lifecycle-r2/` 同矩阵的 process/raw/clock-restore 均通过，三阶段
  passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed=true、
  diagnostic_continue=false。32 个 SD 原始段的 hash 与完整 capture 重建一致；包配对、
  follower owner 边界及活动 mailbox CRC 通过，不能外推为全局同一圈更新。
  RX 增量为 19,130/19,130/19,135/19,134，transport 错误无增长。TDMA 历史最大值为
  2,801.252/2,711.688/2,829.392/2,954.044 µs，对应预算 380 µs；当前窗口 overrun
  增加 40,888/22,706/27,939/24,721，完整 WCET FAIL。历史最大值包含启动/P3，不能
  单独归因到采集。final-stopped 验证四板当前 build、enabled/started/armed=0、
  config/applied 一致；origin 保留 TX_BUSY=5，follower 为零。
- 补充状态审计 `counter-r1.log` 复现未启用路径的真实缺陷：PREPARING 提前返回将
  对外累计收发计数及序号置零，ring runtime 会复制这些值；内部累计值仍在。
  `status-candidate-r1.log` 在 out/ 的候选副本上复用统一状态序列化后通过回归，尚未
  改变本次 P3 所绑定源码。该缺陷必须修复并重新绑定验收后才可接入实际启动。
- 下一 gate：先完成当前短帧/原始证据复核与状态发布修复，再接 Calibration 重装
  授权、capability/DeploymentGate 和真实 owner 调用。调用边界必须位于 adapter
  接受返回后、下一次旧 TX 决策前；不得直接嵌套已有 service 的 snapshot writer guard。
  `next-runtime-admission.txt` 记录具体落点。正常 ARM 仍使用旧 origin，未验证
  persona16、自主发车、service blackout、拥塞或同圈更新；完整 Core1 WCET、正式 RAM、
  硬件 mailbox 授权/参与证据和绝对 VDC 时间继续开放，不提交未启用的独立 builder。

### TDMA-PROGRESS-20260912-007 - origin 物理层与异步 adapter 候选集成、当前源码回归

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交工作树，
  证据根为 `out/HardwareAcceptance/20260911/tdma-flight-origin-integration/`。
  以下 build、时序、资源和测试数字均为实验快照，非事实源；验收范围由
  `hardware-review-r4.json` 和原始报告限定，registry 状态不变。
- `tdma_origin_plan_build()`、`tdma_origin_exchange`、物理 startup、互斥 workspace
  与 persona 资源管理已编译进候选。origin 独占 executor/sniffer 经统一角色声明，
  STOP 保留完整 DMA 依赖树的 BUSY/ABORT 约束；不借 PIO0/DMA7。workspace ARM
  大小为 7,696 B，origin 使用 7,480 B；所有支持的节点位置已检查构图容量。
- runtime 绑定真实物理回调，adapter 新增自主态：仅已接受的 bootstrap boundary
  可以交接；失败保留清理责任直到 common STOP 成功。自主 service 每次最多收割一份
  RX 与尝试一份本地 shadow，硬件 pending 时保留后续 FIFO descriptor。返回包先核对
  所属 bank 的 sequence/identity/generation、完整本地 mailbox 和活动 mailbox 元数据/CRC，
  再进入 receive-health/FIFO；driver generation 与完整 owner generation/sequence
  分别关联。观察缺口只计为未观察周期，不伪造 timeout、SENT、ACK 或已完成窗口。
  旧绝对时间和 DPLL trailer 在交接时失效。正常 ARM 尚无 `start_origin` 调用者，
  本轮硅上回归仍走旧 origin，不能声明自主飞行或 service blackout 通过。
- 软件：`adapter-host-r2.log`、`origin-owner-r6.log`、`comm-fsm-r1.log` 通过；实际 C
  adapter/engine/FIFO/FSM 覆盖跳圈、非法配对、pending publication、失败 STOP 与重启拒绝，
  physical startup 使用 mocked MMIO。`pytest-r6.log` 为 143 通过。build-r4
  `20260911231115` 构建及 Flash 链接检查通过，源码指纹为
  `1f61640c5cc4babc42db526f62bf853c1332529a0453554f0356987a81879115`。
  `ram-r4.log` 正式 RAM FAIL：剩余 3,996 B，低于门禁 49,152 B；不作 RAM 豁免。
- `p3-r2/` 四板真实 OTA 成功，但拓扑阶段未检测到 NO4→NO1，P3 退出失败且未生成
  成功凭证。`failed-pair-r1/` 只做一次同参数邻接重现：接收 DMA 增加 44,850 words，
  两端原始 SD 时钟均有 2,392 个上升沿；它证明该次恢复了物理活动，未解释原始失败。
  两节点邻接 probe 没有完整 DATA feedback，通用 burst 的 passed=false 仍保留。
  位解析以 `pad-edge-review-r2.json` 为准；旧版误用 profile 顺序解释 levels 位，
  修正为 pin_base 连续位后复核，原错误报告保留。
- `p3-r3/` resume 复核当前 build 并复用 r2 的真实 OTA 记录，拓扑、CLK/marker、
  频率阶梯、TRN00/01/02、新 TRN03 矩阵与 TDMA 流程通过。
  `p3-receipt-r3.json` 为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，passed=true、
  strict_gates_passed=true、diagnostic_failures=[]；不覆盖 NO5/DPLL、正式 RAM、完整
  Core1 WCET 或自主 origin。前序 build-r3 的校准/启动失败继续保留，不被此凭证覆盖。
- `lifecycle-r2/` 使用本轮 r3 矩阵，process→raw→带 clock training 的 process 三阶段
  passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed 均为 true，
  diagnostic_continue=false。四板各自完整采集并导出；32 个原始 SD 段的 SHA-256
  均匹配，重新解码后与完整 capture JSON 逐值一致。`owner-audit.json` 的包配对、
  follower owner 边界与活动 mailbox CRC16 均通过。各板独立窗口不能证明全局同一圈更新。
- `schedule-audit.json` 中 RX 分别增加 17,627/17,629/17,632/17,620，transport 错误
  无增长。TDMA phase 历史最大值为 2,661.148/2,477.512/2,860.680/2,966.344 µs，
  预算 380 µs；本区间 overrun 分别增加 37,019/20,540/25,118/22,697，完整 WCET
  仍失败。最大值包含前序启动/P3，不能单独归因到 SD 采集。`final-stopped.json`
  验证四板均为当前 build、enabled/started/armed=0、config_seq=applied_seq；origin
  保留 physical TX_BUSY=5，其他节点为零，不把历史错误清除成无错结论。
- 下一 gate：`next-admission-review.txt` 明确 Calibration 窗口、profile/epoch、资源
  capability 与分步启动边界。当前矩阵的 guard_cycles=0 不是 origin DMA 重装授权；
  cadence 的 guard_floor_ticks 也不是总线仲裁上界。同步 startup 的构图/停止/切换
  尚无完整 Core1 WCET 证明，需先拆为有界 owner action，再接入实际调用并取证
  persona、自主运行、service blackout、迟到更新/拥塞和 fault/STOP/restart。
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，不提交未完成启用的独立 builder 切片。

### TDMA-PROGRESS-20260912-006 - STOP 依赖链、清理结果传播与停止态调度竞争

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `541b512`，代码提交
  `237bfba`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-stop/`。本项数字均为实验快照，
  非事实源。origin C builder/PIO 候选完整保存在 `pending-origin/components/tdma/`，
  尚未安装；本切片不取得 executor、sniffer、PIO0 或 DMA7，也不声称自主 origin 已完成。
- 物理 STOP 在暂停自有 SM 后按 loader 到下游 output/capture 停止，每层重新清 EN，
  检查自有 ABORT/BUSY 并共用既有超时预算。失败不清 FIFO/IRQ、不回收池/资源；
  persona release 同时检查 capture，即使不存在 loader 也不得提前放行。
  coded/MARK/DATA 训练入口和显式 STOP 经过同一停止边界。
- 初版 build `20260911203945` 的独立 process/raw 检查通过，训练后恢复在 NO2
  TRAIN512 超时；`after-training-failure.json` 与 `stop-retry-no2.txt` 保留上层已
  STOPPED、物理 armed 仍置位且 PERSONA_BUSY 的失败。根因是 void STOP 丢失底层
  拒绝，上层随后丢弃清理入口。修复将 physical/adapter STOP 改为 bool，runtime
  保留 context 和显式 pending 并由 owner 重试；停止配置 applied ACK、新 ARM、
  adapter 重绑定与 profile 元数据替换均不能越过未完成 STOP。部分 ARM 的残留图
  同样保留清理入口，配置序号回绕不替代 pending 状态。
- build `20260911205929` 的 `p3-r2/` 完成真实四板 OTA 和新矩阵 process-image/FIFO
  检查；粗 CLK 校准拓扑读回不一致，凭证仍为 QUICK_DIAGNOSTIC、strict=false。
  `lifecycle-r2/` 的 process 通过，raw ARM 在 NO3 以 result=8 拒绝；一次有界重试
  `lifecycle-r3/` 在 reference ARM 同样拒绝。该码属于 RefMem ARM runtime config，
  不能解释为物理错误码。NO3 配置序号推进两次，与 service 配置后 scheduler resume
  失败撤回分支一致；失败后 physical armed 为零且停止 ACK 完成。
- 审计发现已关闭的 traffic admission 仍使 Core1 每拍取队列锁、刷新周期并扫描空队列。
  `traffic-before.log` 的真实 C 负测复现停止态推进周期与争锁；修复以原子关闭提示
  提前返回，开放提示仍须取锁后复核，队列所有权不变。`traffic-after.log`、
  `service-r3.log` 通过。它消除停止态 consumer 的无效竞争，不承诺任意并发控制
  操作永不拒绝，也不替代后续完整 owner mailbox 治理。
- 软件：`pytest-r6.log` 为 153 通过，包含真实 STOP C 例程的晚写入、独立 ABORT/BUSY、
  共享超时与重试；runtime/adapter/service C 用例覆盖失败清理、ACK、重绑定与回绕。
  `pytest-r1-setup.txt` 保留目录准备失败，`pytest-r4.log` 保留签名断言未同步的失败。
  build-r3 `20260911211655` 编译及 Flash 链接检查通过；`ram-r3.log` 正式 RAM 仍
  FAIL，剩余 2,660 B，低于正式门禁 49,152 B。
- 当前源码 `p3-r3/` 完成真实四板 OTA/校准/环路，凭证绑定源码指纹
  `bf3ea067356e6213204cf5b45ad6d4e7d3118ca3b2f414d3df26ccf62e3c0e28`。
  校准阶段均通过，启动屏障在两秒内仅取得所需两次中的一次稳定采样，故 strict=false；
  首次采样保留初始 receive/bitmap 拒绝增长，第二次及后续 soak 通过。该失败不能
  隐去，也不能据此直接归因为矩阵错误。QUICK_DIAGNOSTIC 流程完成不代表产品验收。
- `lifecycle-r4/` 使用明确记录的前序实测对照矩阵与既有四秒启动窗口，process→raw→
  带 clock training 的 process 三阶段均通过，closed-loop/realtime 门禁通过且
  diagnostic_continue=false；本轮无 ARM 拒绝。四板各自有限采集的 SD 原始段可完整
  重建 JSON；`owner-audit.json` 接受每板同钟入/出包、逐 follower 字节所有权、头部
  完整性和活动 mailbox CRC16。对照矩阵不是本轮校准证明，各板局部包对也不是全局
  同一圈更新或 service blackout 证明。
- `schedule-audit.json` 中各板采集/导出期间增加约 1.94 万接收帧，transport 错误无
  增长，但 TDMA phase 历史最大耗时约 2.396–2.930 ms，超过 380 µs 预算，超限计数
  继续增长；历史最大值包含前序 P3/启动，不能归因为纯采集开销。完整 Core1 WCET
  仍失败。`final-stopped.json` 验证四板均为当前 build、physical armed/runtime started
  为零，STOP 配置等于 applied，analyzer RELEASED、SD job DONE。
- 下一 gate：恢复完整 origin builder、资源准入、异步 completion 与安全内存生命周期
  集成，再执行新的当前源码 P3/原始波形与真实 service blackout。正式 RAM、严格 P3
  启动门禁、完整 WCET 与 C11 仍开放，`TDMA-FLIGHT-002B` 保持 IN PROGRESS，registry 不变。

### TDMA-PROGRESS-20260912-005 - origin 完整执行图、交接竞态与固定窗口候选

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `e87eea4`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-frame/`，入口为
  `frame-manifest.json`。本项数字均为离线实验快照，非事实源。生产源码未改，未执行
  硬件查询、OTA、ARM 或新的 P3；既有单板合入与上一固件验收来源保持独立。
- `candidate.pio` 与真实既有 DATA/RTT/latch 程序联合汇编，两个 TDMA PIO 均恰好
  占满 32 words。helper PACK/COMPARE 无 GPIO，候选独立 DMA8 执行器处理本地
  CONTROL 边界，DMA6 装载其 AL3，DMA4/5 分别捕获/输出；DMA8 与全局 sniffer
  尚未声明或获得资源准入。模型通过 19,968 个 helper、876 个 capture、27 个
  截断输入和 13 个重定位串接用例；不占用 PIO0 或 DMA7。
- `origin_graph.py` 生成完整有限图，修复常量零与未解析分支地址共用存储的错误。
  字宽捕获候选含 226 个 descriptor、77 个 literal，逻辑占用 3,924 B，fixture
  预留 4,608 B；这不是固件 RAM 准入。`check_graph.py` 执行 AL3、拆分的 DMA
  读写事件、有限 FIFO、真实 helper 指令、sniffer 和子通道中止/重装，输出与本仓库
  编译的 transport C oracle 一致。257 帧涵盖缺包、半包、坏 identity/transport、
  错 hop、旧 sequence 与错误 profile；四种中止不收敛/总线错误进入稳定 FAULT，
  保留资源。本地边界可在 DATA 停顿时继续；抽象步数不能换算成硬件周期上界。
- `check_graph_races.py` 覆盖最后一次捕获写入晚于剩余量快照的 16 种交错；即使
  后续写入完成，已记录的半包仍不被接纳。128 圈并发发布完成 128 次旧 shadow 池
  回收；RX 版本检查接受 68 份一致副本、丢弃 62 份跨复用观察。八种描述符变异均
  被所有权检查或 C oracle 检出。另保留未等替代 generation 就复用池导致混合 mailbox、
  以及无界挂起 reader 遭版本回绕 ABA 的反例；生产内存屏障、读取上界和 STOP/debug
  epoch 仍待实现，不能仅用 seqlock 版本相等宣称跨任意停顿安全。
- 窄访问候选：RX FIFO 低字节直接进入紧凑银行，省去 PACK；TX 使用 halfword
  高字节和 DMA stride。真实 DATA 指令的 36 组表示对照与 65,536 组 padding 检查
  通过。紧凑图加入四个显式活动槽的 CRC16 后为 224 个 descriptor、87 个 literal，
  逻辑占用 3,932 B；与真实 mailbox CRC16/transport C 一致，接受 32 个有效返回，
  拒绝覆盖各活动 mailbox 全部字节的 128 个损坏用例。字宽/紧凑路径在 17 帧中一致；
  八活动槽超出此 fixture 的 descriptor 上界而被拒绝，不作为新的产品容量限制。
  窄 FIFO 读写、CRC16 寄存器高位及真实总线行为仍需硅上验证；CRC 正确的错误
  source/target/class、逐圈参与与 V2 尚未闭合，DPLL trailer 当前明确无效。
- 节拍缺口：当前 AL3 图在校验分支结束后立即发车，不能证明固定帧间隔。
  `guard_candidate.pio` 将 bit count 与准备窗口 count 放入启动字，在 CS 撤销后
  先发布边界 token，再等待固定 PIO 窗口；替代 CONTROL/capture 各 10 words，
  PIO1 联合布局仍为 32 words。16 组窗口/长度与 144 组正 prefix 指令测试通过；
  迟到供给确实延长节拍的反例也保留。该窗口未与 AL3 图合并，须由 Calibration
  发布预算并完成供给最坏上界及有界迟到故障策略，不能只加等待后宣布 F2/F3 通过。
- 下一切片按 `working-design.txt` 集成完整 C builder、地址/PC 白名单、DMA8/sniffer
  仲裁、启动 alignment、异步 adapter completion 和 owner 生命周期，先审计所有
  calibration/RX/TX/follower workspace 的互斥性，再考虑复用内存。固件实现后执行
  软件/构建/当前源码 P3/四板原始波形闭环。上一 strict=false、正式 RAM 和完整
  Core1 WCET 失败仍开放；真实 service blackout、DPLL latch 与 C11 尚未完成。
  本项执行离线回归及文档门禁，`TDMA-FLIGHT-002B` 保持 `IN PROGRESS`，registry 不变。

### TDMA-PROGRESS-20260912-004 - reference 头部计算候选与返回映像交接审计

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `dd5d8c0`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-audit/`。本项数字均为离线实验
  或历史快照，非事实源。仅审计并更新文档，产品源码未改，未执行硬件查询、OTA 或 ARM。
- 当前依赖：reference 每圈仍由 Core1 构建 TX words、准备返回映像、更新 sequence/CRC
  并向 CONTROL FIFO 写入发车字；DATA 长度供给、TX completion 收割、clock-latch
  重装和 RTT seed 也不能直接作为 Core1 缺席下的持续路径。真实 pioasm 汇编得到
  origin 当前 PIO1 程序合计 25 words、PIO2 合计 12 words；PIO1 的四个 SM 均已有
  owner。空余指令不等于空余 SM，更不证明新增握手程序已经通过准入。
- `origin_header_model.py` 使用 SDK 的 RP2350 寄存器值编码候选 AL3 描述符，并以
  本仓库实际编译的 `tdma_transport_frame.c` 为独立 oracle。SUM 执行 sequence 加一，
  CRC32R 与输出反转/取反生成 identity 和 transport CRC；identity 排除 hop_count，
  transport CRC 字段按零输入。21,280 个用例及同图连续 2,048 次跨回绕一致，公开
  CRC check vector 与错误链、越权地址、未对齐、无界 count、DREQ 和 CRC 覆盖负测
  通过。计算图只写 reference header 指定字段，payload 保持不变；这不是返回包验证。
- 候选子图含 15 个 descriptor、逻辑工作存储 300 B，每次 66 个 output transfer
  和 60 个 loader word transfer，见 `header-model-report.json`。这些是描述符模型
  的事务数与存储量，不是 DMA 周期上界、固件 RAM 余量或完整 origin 资源预算。
  该图没有接入固件，末尾的模型重触发也不是可直接装板的物理发车程序。
- `return_ring_audit.py` 按当前 RX ring 符号和上一切片 physical frame 快照复现四类
  反例：固定地址读到上一旧帧、回绕后混帧、reader 落后遭覆盖、未完成帧发布混入旧尾部。
  在历史 301 words/frame 和当前 1,024 words/ring 下，frame start 遍历全部 ring
  位置才重复。另有 308,224 个起点/word 位置检查证明：若硬件另行保证完整固定帧
  cursor 及最多一个后继帧写入，则上一帧与下一帧不重叠。该有条件的内存结论不包含
  cursor 实现、真实 bit alignment、缺失/额外 clock 或截断返回的恢复证明。
- HAOFV 准入缺口：DMA6 command contract 目前仅适用于 process follower，资源仲裁
  尚未声明全局 sniffer；二者必须先显式声明及冲突验证。下一实现先完成硬件帧位置与
  返回映像交接，再联合证明 CONTROL/DATA/capture 边界、硬件完成及 sequence 关联。
  DMA selection 仍不能当作 wire completion、ACK/fence 或 DPLL 时间。完整方案和
  下一切片退出条件见 `design-audit.txt`。
- 验证范围：本项执行离线模型、C oracle 编译、现有 PIO 汇编和文档门禁；未产生新的
  P3 固件验收或多板波形。上一切片的 strict=false、正式 RAM 与完整 Core1 WCET
  失败继续保留。`TDMA-FLIGHT-002B` 保持 `IN PROGRESS`，registry 状态不变。

### TDMA-PROGRESS-20260912-003 - follower 自主续转、池交接与启动对齐修复

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/006/007`。本项数字均为实验
  快照，非事实源。基于 `9b8d5f7`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-follower-recurrence/`。
  最终 build `20260911182033`，源码指纹
  `da774652f2a50cb812c32efc57d8afba4b515458fc787447ca54474744112bd4`，共 979 个源文件。
- 固件：既有 DMA output/loader 通过 selection、数据段和 restart 控制段持续执行。
  Core1 仅初始化 loader，后续完整构建 inactive plan 后发布 SRAM successor 指针；
  单 pending publication 被 DMA 选择后才回收旧池。未就绪不 acquire TX，无新版本
  不重建计划，失败不更新接受版本。资源仍由 TDMA owner/Resource Arbiter 管理；
  PIO 指令、SM、RX FIFO owner 不增加。selected generation 可能领先物理 CS，
  只作内存交接证据，不是 SENT、wire completion、RefMem ACK/fence 或 DPLL 时间。
- 首轮真实失败：build `20260911180139` 的 P3 和独立复验均保留。既有校准矩阵下
  三个 follower 接收连续，但 reference 持续报 `BAD_ROUTE`。`round1-node*/` 的 SD
  原始采集及 `return-failure-decode.json` 证明 NO2 输入头 CRC 正确，输出将
  `hop_limit` 从 3 改为 131，CRC 也错误。`diagnostic-running.json` 显示首帧 bit
  shift 为 1，随后 10293 个包为 zero shift；第一次修改计划过早锁定了启动瞬态。
  `diagnose-return/` 明确使用 diagnostic continue 取证，未计为通过。
- 修复：ARM 后 PASS 由硬件重复执行；相邻完整 physical frame 的 byte/bit alignment
  连续一致，达到 `TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES` 后才允许首份
  修改计划。之后锁定该 ARM epoch 的槽位，解析副本丢失不得再次移动线上位置。
  真实 C scanner 回归覆盖首次 `1 → 0 → 0`、新 epoch 与锁定后的副本丢失；
  当前波形确认 NO2 正确推进 hop 且不再改动 hop_limit。持续异常初始流和真实时钟
  丢失的恢复证明仍开放，稳定样本门槛不能替代全部环境故障验证。
- 软件：bit/CRC/资源/字段解析 79 项通过；DMA/scanner/TRN-03 最终 112 项通过。
  实际 DMA 绑定函数的 host bus 验证连续 200 份计划、48 个发布时间点、池回收、
  generation 回绕、段间 idle、迟到 trigger、STOP 超时与重试；C adapter 覆盖无更新、
  busy 不取走 TX、失败后重试及无 TX bootstrap。C owner/alignment/value 163840 组
  通过，ResourceArbiter 负测通过。首轮 fixture 误提取前置声明和旧 one-shot/PASS
  静态断言失败均保留，已同步修正。命令及日志见 `software-verification.json`。
- 构建与 P3：Release 与 A/B/Boot Flash gate 通过。`ram-release-r2.log` 的 free
  为 2668 B，正式阈值为 49152 B，正式 RAM 门禁失败。两轮都执行真实 P3 `run`；
  最终 `p3-r2/receipt.json` 仍为 quick diagnostic，`strict_gates_passed=false`。
  coarse ARM/map、coded marker、SCK training/replay 和默认 2 秒 startup 屏障失败
  原样保留；runtime 稳态各 Node 与最后 handoff 通过不覆盖这些失败。
- 独立闭环：新矩阵的 SCK 行重装余量为负，独立工具在硬件动作前拒绝，日志为
  `controlled-fresh-matrix.log`。`controlled-prior-matrix-fixed/` 在当前新固件上使用
  前序 live-CRC 切片中满足重装预算的既有矩阵，通过 passed/closed_loop_passed/
  realtime_gate_passed，diagnostic_continue=false；沿用受控 4 秒 startup 窗口。
  既有矩阵本身有 diagnostic provenance，不能称为本轮新校准或严格产品验收。
- 沿途复核：`round2-owner-audit.json` 绑定四板完整同钟 packet 对及 SD 原始页，
  三个 follower 均无越权 byte，全部 active mailbox CRC16 有效；`round2-byte-phases.json`
  的有限窗口各 bit 位置输出 phase 均为 88 ns，采样量化为 8 ns，不是 DMA/环境最坏证明。
  采集继续使用既有 1024 B 下载页参数。`handoff-audit.json` 中发布和选择 generation
  持续推进，无新增准备失败；分次 SCPI 读数及 sticky IRQ3 不能换算成精确圈数或
  无 Core1 service 的证明。
- 完整调度：`schedule-audit.json` 区间每板接收约 28800 帧，基础 transport 错误
  增量为零；TDMA overrun/deadline 仍增长，累计 max 为约 2408–3328 us，WCET
  为 380 us。累计峰值包含启动事件，不把差异归因于单操作，也不据短帧工具通过
  宣称 HAOFV 完整 WCET 已满足。
- 生命周期：`raw-transition/` 与 `restored-process-training/` 在同一既有矩阵和当前
  build 下均通过非强制闭环；后者覆盖 clock training 后恢复 process。最终四板均
  STOPPED、analyzer RELEASED、SD DONE，见 `final-stopped.json`。成功与首轮
  失败、旧/新矩阵来源及最终源码指纹由 `slice-manifest.json` 分别绑定。
- 后续边界：reference 仍由 Core1 逐帧准备并写 CONTROL FIFO；自主 sequence/identity
  CRC、返回映像保留和硬件发车是下一独立切片。当前 LOAD MASK 不能屏蔽 mandatory
  TDMA service；未执行完整 Core1 blackout。正式 RAM、完整 WCET、严格校准、尾部
  完整性、最坏 DMA/环境和 HAOFV-879/C11 继续开放，登记状态不变。

### TDMA-PROGRESS-20260912-002 - 可跨 sequence 复用的 follower header 计划

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/006/007`。本项数字均为实验
  快照，非事实源。代码提交 `7259cb4`，基于 `2f6efd5` 并保留已有单板合入；证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-live-crc/`，入口 `slice-manifest.json`。
  build `20260911172347`，源码指纹
  `fca9003c2f528f9a5916e56622887edb38bd687c21b73ae69c9063b78022514c`，共 979 个源文件。
- 固件：header 的 hop/CRC 由绝对值 REPLACE 改为对实际在途 bit 应用 XOR 差分，
  消除计划对预测 sequence CRC 的依赖；本地 mailbox 仍使用 owner 准备的值。
  process follower 将 ISR 改为右移，内部 LIVE/INVERT 分别选择当前 bit/反相 bit，
  DATA 程序仍为 28 条，PIO2 总预算仍为 32 条，完整 byte 时序预算不变。adapter
  仅对该 persona 的 RX observation copy 恢复 bit 顺序，raw/reference 表示保持原语义。
  header mask 也不能扩大到 sequence 等未授权字段。
- 软件：当前真实 C plan、transport encode/advance、pioasm 指令与实际 adapter RX
  归一化函数联合测试 44 项通过，DMA/资源套件 30 项通过，PIO patch 测试 9 项通过。
  C runner 保持 163840 组 owner/alignment/value 组合通过。固定计划覆盖全部合法 hop、
  sequence 基向量与回绕；所有 header 单 bit 错误保留原 CRC syndrome，旧绝对 CRC
  重放为负例。首次 DMA 测试因新增 transport helper 链接依赖缺失而失败，已同步修正
  Python fixture 与 C runner；原失败日志 `model-tests-r1.log` 保留。模型仍不证明 DMA
  仲裁/断粮最坏情况，也不证明硬件自主重复执行。
- 构建与资源：Release build、A/B/boot Flash gate 通过；双 plan 池和静态 RAM 占用
  未增加。`ram-release-r2.log` 中 free 2788 B，正式阈值 49152 B，正式 RAM 门禁仍失败。
- P3：真实 `run` 完成四板 OTA、校准和 quick diagnostic 流程，归档凭证为
  `p3-r1/receipt.json`，`strict_gates_passed=false`。coarse CLK 阶段 NO4 ARM/status
  查询超时，以及默认 2 秒 startup barrier 只取得一个稳定样本的失败均保留，尚未证明
  ARM 超时根因。不得把 quick diagnostic receipt 的 passed 解释为产品验收通过。
- 独立复验：`controlled-fresh-matrix/` 使用本轮生成的 replay matrix，在既有受控
  4 秒启动窗口下通过 passed/closed_loop_passed/realtime_gate_passed，
  diagnostic_continue=false；矩阵自身保留 diagnostic provenance。startup 首窗错误
  仍在原始记录，稳定屏障后才统计无新增基础错误。这不修复或覆盖 P3 默认窗口的失败。
- 沿途复核：`round1-owner-audit.json` 绑定四板原始 SD capture，每块板均找到完整同钟
  packet 对，三个 follower 窗口均无越权 byte 改动，全部 active mailbox CRC16 有效。
  `round1-byte-phases.json` 的各 physical bit 位置均观测到 RX SCK 上升后 88 ns 输出，
  采样量化为 8 ns；仅是当前有限窗口，不是同步器/DMA/环境最坏延迟证明。采集使用已有
  1024 B 下载页参数，四板导出通过，不声称修复前序大页串口截断问题。
- 完整调度：`schedule-audit.json` 的区间内各板 RX 增量约 23000，基础 transport 错误
  增量为零，但 TDMA overrun/deadline 继续增长；累计 max runtime 为约 2387–2930 us，
  phase WCET 为 380 us。累计最大值含先前启动事件，不把差异归因到某一个操作。
  `full_haofv_wcet_accepted=false`，独立短帧工具的 realtime gate 不能覆盖这个失败。
- 生命周期：`raw-transition/` 与 `restored-process-training/` 在本轮矩阵下均通过非强制
  闭环，后者覆盖维护 clock training 后恢复 process。初次 raw 命令误带仅适用于 process
  的参数，被主机解析器拒绝；纠正后执行，原日志 `raw-transition.log` 保留。最终四板
  build 一致、STOPPED、capture RELEASED、SD DONE，见 `final-stopped.json`。
- 下一 gate：推进 follower descriptor recurrence 和不可撕裂的 generation 发布。
  reference 的 CONTROL FIFO 发车、返回映像保留、sequence/identity CRC 更新
  是另一独立门禁；不得以旧帧固定重放代替持续循环。未冻结的后续设计、资源风险和
  DMA 预取选择与 wire completion 的区别保存在 `next-slice-notes.txt`。
  当前仍由 Core1 逐帧供给；B1、自主循环、完整 WCET 与长期目标均未完成，登记状态不变。

### TDMA-PROGRESS-20260912-001 - bit 沿途更新与稀疏 DMA 集成验证

- TODO task ID：`TDMA-FLIGHT-006/007`。本项所有 build、数量、容量和时序数字均为实验
  快照，非事实源；证据根为 `out/HardwareAcceptance/20260911/tdma-flight-owner-bit/`，
  入口 `slice-manifest.json`；软件初检另见同日 `tdma-flight-owner-bit-plan/`。保留另一设备的 UI/单板提交
  `d5ed7bc`，在 HEAD `708e59b` 上集成；build `20260911164308` 对应源码指纹
  `e55b1f78fddf0662af69ef373c15f80d4a495dc6f4a65259d7a67d242213b230`，共 979 个源文件。
- 固件：process follower 改用内部 LIVE/ZERO/ONE token，header 与 local mailbox 为
  稀疏存储窗口，窗口本身不授予修改权。头字段仅允许 hop/transport CRC，其他 slot、
  DPLL trailer 和越界 bitmap 被拒绝。两个版本的 plan 包含各自描述符和 token；最终
  descriptor 自 chain 以终止装载，段间 output 空闲不能触发池复用。loader 属于已有
  DMA_FORWARD 仲裁资源，独立 SDK claim，STOP/回滚/维护切换停止加载与输出，超时保留
  资源。内部 ARM 初始化 ISR 并 prime OSR，跨帧保留 ISR，RX FIFO 副本丢弃不阻塞流水。
- 软件：C 单测通过 163840 组所有权/对齐/数值组合，另验证越权 header/slot/trailer、
  force 等值更新、非法 token/terminal/descriptor/tail。新测试用实际 C plan 和 pioasm
  指令字解释双帧，覆盖装载偏移、RX 丢弃和完整 byte 时序正/负测。模型套件 73 项通过，
  后续 DMA/资源/TRN 套件 159 项通过；DMA host bus 验证 AL3 段间空闲、终段禁止越界
  chain、未 claim/损坏 plan 拒绝，以及加载写竞争、停止超时与重试。模型不包含真实
  DMA 仲裁延迟。相关 C transport/resource tests 与 warnings-as-errors 构建通过。
- 资源：实际 DATA 程序 28 条，配合原 clock-latch 4 条占满 PIO2；plan 双池 2296 B，
  SRAM LIVE 常量另计。最终 linker free 2788 B，低于 release 要求 49152 B，正式 RAM
  门禁失败保留，不能用稀疏池装得下代替产品余量通过。
- P3：`p3-r1/receipt.json` 是本轮真实 `run` 的归档副本，quick diagnostic 流程完成，
  `strict_gates_passed=false`。coded marker/TRN-01 失败、新 SCK 行 re-arm 余量为负、
  NO4 ARM result 8 与无 running handoff 均保留。对新矩阵执行非强制复验又被主机 SCK
  门禁拒绝，见 `controlled-r1.log`，没有修改矩阵数值来放行，也不将 ARM result 8 的
  根因等同于该 SCK 失败。
- 受控闭环：使用前序已验证的合法 baseline matrix、当前新 build 运行
  `controlled-baseline-matrix/`，passed/closed_loop_passed/realtime_gate_passed 均为 true，
  diagnostic_continue=false。该矩阵复用只用于相同夹具/profile 的固件对照，不代表新
  calibration gate 通过；startup 首窗出现填充期拒绝，稳定屏障之后未新增基础错误。
- 生命周期：`raw-transition/` 证明新 process persona 停止并释放 loader 后可进入 raw；
  `restored-process-training/` 再恢复 process、执行可选 coarse clock training 的维护切换
  并重新启动短帧，二者均在非强制模式通过。最终四板 STOPPED、capture RELEASED、
  SD job DONE 的读回见 `final-stopped.json`；该终态绑定本轮 build。
- 沿途证据：`round1-owner-audit.json` 包含三个 follower 窗口，`round2-owner-audit.json`
  包含四板完整包。每个 follower 的输出只改变授权 header/local mailbox，所有 active
  mailbox CRC16 均有效，DPLL trailer 逐 byte 保持。相同审计在旧 R3 窗口得到
  `follower_owner_boundaries_accepted=false` 和 CRC 失败，见 `old-r3-negative-control.json`。
  每个窗口均由 SD bin 重建，并以完整 transport header CRC、sequence/hop 和同钟
  RX/TX 重叠配对；FLIGHT_MUTABLE 的 transport CRC 不覆盖整个 payload。
- 时序：两个 `round*-byte-phases.json` 按实际 RX SCK 上升沿及 physical byte 位置分组。
  第二轮全部观测位置为 88 ns，第一轮有一个位置出现 96 ns；原 byte 首 bit 的系统性
  延后未在本次窗口复现。8 ns 采样量化、有限窗口、同步器绝对偏移和 DMA 压力限制均
  保留；不把观测 phase 写成新的产品常数或最坏延迟证书。
- 下载失败：两轮 NO1 的默认大页读取均返回截断 hex，采集/SD 写入已经完成。原失败
  summary 保持原样，`recovered-round2-node0/` 通过现有小页接口重新读取相同 epoch，
  校验每段 tag/build/CRC 并恢复完整 capture；没有重采或伪造缺失字节。该问题仍属待
  归因的主机/串口导出限制。
- 完整 WCET：`schedule-audit.json` 区间内各节点 RX 增量约 63000，基础 transport 错误
  增量为零，但 TDMA phase overrun/deadline 计数持续增长；累计 max runtime 约
  1768–2584 us，合同 WCET 为 380 us。独立短帧工具的 realtime gate 不覆盖完整 TDMA
  WCET，不能据其通过声称消除了 Core1 的逐帧供给成本。本切片尚无自主发车/续装和
  generation boundary handoff；B1/B2/B3、完整 WCET 与严格资源/校准仍待完成，契约
  登记状态不变，长期目标继续 active。
