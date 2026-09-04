# SYNC_IO 任务进度

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_TASK_PROGRESS.md`
Related: `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/sync/SYNC_IO_TODO.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`, `docs/storage/LOG_SYSTEM_TODO.md`
Last updated: 2026-09-05

本文档只记录 SYNC_IO 域的提交、构建、测试、OTA/HIL、失败、回退和证据位置。任务状态以
`SYNC_IO_TODO.md` 为唯一事实源，稳定语义以 `SYNC_IO_ARCHITECTURE.md` 为准。

## 文档接口

- 每条新记录必须有唯一 `SYNC-PROGRESS-*` ID，并引用一个或多个 `SYNC_*` TODO task ID。
- 架构契约、任务状态和实施证据分别由 Architecture、TODO 和本文件维护，不交叉替代。
- 历史 `SYNC_IO-TASK-*` 记录已迁移为 `SYNC-PROGRESS-*` ID；其中的单次数字都是当时验收
  快照，不是当前代码事实源。

### SYNC-PROGRESS-20260905-008 - analyzer 多段 SD 导出索引与丢样本区间

- TODO task ID：`SYNC-LA-006`。
- 变更：新增 `tools/analyzer_trace_batch_index/analyzer_trace_batch_index.py`，对多个
  `SLAY` analyzer segment 执行离线 decoder 校验，按 session 建立导出索引，保留 payload/file
  CRC，并将单文件 record sequence gap、跨 segment gap 及 header dropped count 展开为显式
  `drop_intervals`；同时提供有界 CSV 索引输出。工具不访问实时固件，不推断 NO5/SMA 外部链路
  健康度。
- 软件验证：batch index、analyzer decoder、NO5 关联器和 SYNC_IO contract 共 16 项测试通过；
  新工具 `py_compile` 通过。
- 构建与硬件证据：本轮沿用当前源码指纹对应的
  `out/build/sync-la-008-correlator-20260905/` release build 与
  `out/hardware-acceptance/sync-la-008-correlator-20260905/` 完整 P3 证据；其中 TDMA
  `cycles=4096`、`passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`。
  新增工具为离线 host 路径，未改变固件镜像；此前 quick diagnostic receipt 仍用于 staged
  指纹门禁。
- 边界：真实 SD 批量文件和跨 session 同窗数据尚未在现场取得；本工具完成了索引/区间语义，
  但 `SYNC-LA-006` 仍保持 `IN PROGRESS`，待真实批量导出和长期背压/drop evidence 验收。

### SYNC-PROGRESS-20260905-009 - decoder 与批量索引共享 drop interval 事实源

- TODO task ID：`SYNC-LA-006`。
- 变更：基础 `analyzer_trace_decode.decode()` 现在直接输出 `drop_intervals`，统一表达
  record sequence gap 与 header dropped count；批量索引复用该字段，并额外补充跨 segment
  gap。这样 JSON、CSV 索引和后续 SVG 标注不会各自推导丢样本语义。
- 软件验证：analyzer decoder、batch index、NO5 关联器和 SYNC_IO contract 共 17 项测试通过，
  `py_compile` 通过。此前 P3/TDMA 证据仍适用于本次仅离线工具的增量。
- 边界：现场仍缺真实 SD 批量导出与长期背压窗口；`SYNC-LA-006` 继续保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260905-010 - EDGE_TIMESTAMP 后端与 TDMA 无扰动闭环

- TODO task ID：`SYNC-LA-003`、`SYNC-LA-007`。
- 变更：`sync_io_logic_analyzer_hw_service()` 支持 `EDGE_TIMESTAMP`，维护上一采样电平，
  仅在 source mask 内检测到 level change 时发布带 hardware tick、edge mask 和原始 sample
  的 record；ARM 时清零 edge 状态并按配置 sample period 设置 PIO 时钟。新增 host contract
  断言覆盖 edge-only 发布路径。
- 软件验证：`test_sync_io_logic_analyzer_contract.py`、文档回归测试共 27 项通过；
  `tools/tests/run_sync_io_logic_analyzer_tests.ps1` 通过。`pico2-release` 双应用/boot
  构建、UF2/package 生成及三份 flash-link contract 检查通过，package 使用已验证的
  4096-byte OTA stream block，build `20260904221356`。
- 硬件验收：五板 OTA 与 P3 初始化/四板 TDMA 验收使用当前源码指纹完成；TRN-03B
  process-image `cycles=512`、`passed=true`、`closed_loop_passed=true`、
  `leave_running=true`，证据位于
  `out/hardware-acceptance/sync-la-004-edge-timestamp-current-20260905/trn03-process-image/`。
  在 ring 保持运行窗口分别对 COM3/COM4/COM5/COM6 执行 analyzer ARM/STOP，四份结果均
  `passed=true`，TDMA RX/TX sequence 持续增长，bad/transport/schedule/profile/error 增量为零，
  原始 JSON 位于同目录 `analyzer-control-*-running*.json`。
- HIL 工具同时读取 `ANALyzer:STATe?`，确认 ARM 后 `mode=EDGE_TIMESTAMP` 且
  `active=1`，避免仅凭 mailbox accepted 响应误判模式已生效。
- 失败保留：统一完整 P3 流程在 TRN-00 marker SD 文件写入阶段出现
  `marker capture SD job timeout`，已保留 `trn00-marker.log` 和同轮 P3/OTA 原始证据；该
  诊断失败未触发越界 DMA、非法内存/Flash 或失控 GPIO 硬停。validation 配置另有既存 RAM
  overflow，release 配置已通过并用于板端验收。
- 边界：长时间 wrap、SD 背压/drop interval 和真实 edge timestamp 连续性仍未完成，
  `SYNC-LA-003` 继续保持 `IN PROGRESS`；`SYNC-LA-007` 本轮 TDMA 无扰动门禁完成但 TODO
  状态暂不回填。

### SYNC-PROGRESS-20260905-011 - EDGE_TIMESTAMP SCPI 实际入口与模式确认

- TODO task ID：`SYNC-LA-003`、`SYNC-LA-007`。
- 变更：新增 `REALtime:IO:ANALyzer:EDGE:ARM`，复用异步 intent mailbox 和 Core1
  mandatory service，但构造 `EDGE_TIMESTAMP` 配置；原 `ANALyzer:ARM` 保持
  `RAW_SAMPLE` 兼容语义。SCPI 命令表和接口文档已同步。
- 软件验证：analyzer contract、文档回归和 host logic-analyzer 测试共 28 项通过；
  `pico2-release` 双应用/boot 增量构建、UF2/package 和 flash-link checks 通过。
- 硬件验收：基于 build `20260904221356` 的四板 TDMA process-image 闭环
  `cycles=4096`、`passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`；
  COM3/COM4/COM5/COM6 的 EDGE ARM/STOP HIL 均确认 `ANALyzer:STATe?` 在 ARM 后
  `active=1, mode=2 (EDGE_TIMESTAMP)`，且 TDMA RX/TX sequence 前进、错误增量为零。
  证据目录：`out/hardware-acceptance/sync-la-011-edge-scpi-20260905/`。
  同一 ring 窗口的双 ARM/STOP HIL 还确认两次 `capture_sequence` 不同且非零，
  证据写入 `analyzer-control-*-r*.json`。
- 失败保留：同轮 `--tdma-only` 总结的 `diagnostic_passed=false` 仅因既有 ring
  capture completeness/SD 采集诊断失败；短帧闭环本身通过，未触发不可恢复安全硬停。
- 边界：当前 EDGE 仍需长时间 wrap、真实 edge record 连续性和 SD 背压验证，
  `SYNC-LA-003` 与 `SYNC-LA-005` 保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260905-012 - capture sequence 可追溯性与双 ARM HIL

- TODO task ID：`SYNC-LA-003`、`SYNC-LA-005`、`SYNC-LA-007`。
- 变更：`capture_sequence` 从固定常量改为每次 capture init 单调递增（绕过零值），
  同一序号由 record 与 snapshot 共同发布；`ANALyzer:STATe?` 增加该字段，便于
  Core0/StorageAO 和离线批量导出关联采集会话。
- 软件验证：analyzer contract 11 项、host C contract 通过；`pico2-release` 双应用/boot
  构建、UF2/package 与 flash-link checks 通过，package 使用已验证的 4096-byte OTA block。
- 硬件验收：当前固件五板 OTA 成功；四板 TRN-03B process-image `cycles=4096`、
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，证据位于
  `out/hardware-acceptance/sync-la-012-capture-sequence-20260905/trn03-process-image/`。
  COM3/COM4/COM5/COM6 各执行两次 EDGE ARM/STOP，均确认 `mode=2`、active=1、序号由
  `2` 推进至 `4`，TDMA sequence 持续增长且 bad/transport 错误增量为零；原始结果在
  `analyzer-COM*.json`。
- 失败保留：统一 `--tdma-only` 流程在 TRN-00 marker SD summary 生成处再次遇到
  可恢复 timeout；后续直接使用同轮当前固件的四板 TRN-03 闭环完成短帧门禁，未触发
  越界 DMA、非法内存/Flash 或失控 GPIO 硬停。
- 边界：长时间 wrap、真实 edge record 连续性、SD 背压仍待完成；`SYNC-LA-003`、
  `SYNC-LA-005` 继续保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260905-007 - 本机 analyzer 与 NO5 外部波形离线关联器

- TODO task ID：`SYNC-LA-008`。
- 变更：新增 `tools/analyzer_no5_correlator/analyzer_no5_correlator.py`，读取
  `analyzer_trace_decode` 的本机 SLAY JSON 与 `dpll_waveform_capture` 的 NO5 分析 JSON，
  在记录具备绝对硬件时间时以最近邻和可选有界容差生成配对；输出共同时间基、时间窗重叠、
  中位时间偏移、记录配对及独立 sequence/session 元数据。可选 sequence-anchor JSON 只
  记录外部提供的 TDMA 周期锚点，不把 analyzer `capture_sequence` 与 NO5 `sample_seq`
  当作可直接比较的同一计数器。
- 证据边界：报告固定声明两类证据不可互相替代；本机 analyzer 仅证明 pad-visible 电平，
  NO5 仅证明外部 SMA/线缆波形。没有真实同窗 capture pair 时，不输出外部物理链路通过结论。
- 软件验证：`test_analyzer_no5_correlator.py`、`test_analyzer_trace_decode.py`、
  `test_sync_io_logic_analyzer_contract.py` 共 14 项通过；关联器 `py_compile` 通过。
- 构建验证：`out/build/sync-la-008-correlator-20260905/` 完成 `pico2-release` 配置、
  双应用/boot 编译、UF2/package 生成和三份 flash-link contract 检查；package 使用已验证
  的 4096-byte OTA stream block。
- 硬件验收：五板 OTA 与完整 P3 运行完成，build `20260904184611`；
  `out/hardware-acceptance/sync-la-008-correlator-20260905/diagnostic.json` 的
  `flow_completed=true`，TDMA `tdma-process-image/summary.json` 报告
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`、`cycles=4096`。
  coarse CLK 与 NO5 DPLL 观察仍有既有诊断失败，按调试有界继续策略保留原始状态/原因，未
  触发越界 DMA、非法内存/Flash 或失控 GPIO 硬停。
- 边界：本轮 P3 报告 `SD waveform evidence unavailable`，因此没有真实本机/NO5 同窗文件可
  供关联；`SYNC-LA-008` 保持 `IN PROGRESS`，下一步是取得同一 TDMA observation window
  的两类原始 capture 并提供显式 sequence anchor。

### SYNC-PROGRESS-20260905-001 - Core0 analyzer segment StorageAO 持久化

- TODO task ID：`SYNC-LA-005`、`SYNC-LA-006`。
- 变更：在 `app_diag_service()` 接入停止态 `sync_io_logic_analyzer_drain_core0()`，按有界
  segment 组装固定 header + capture records，经 `storage_manager_begin_evidence_write()`
  排队 StorageAO 写入；文件 CRC 覆盖完整 header 与 payload，session 使用板端 uptime，避免
  重启后覆盖同名证据文件。analyzer 组件本身继续不依赖 StorageAO。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过；analyzer/P3 Python 合计 `38 passed`；
  `out/build/sync-la-002-alias-fix-20260904/` 增量固件编译、UF2/package 生成和 flash-link
  checks 通过。首次编译因缺少 `board.h` 显式 include 被捕获并修复，未绕过门禁。
- 硬件验收：四板 OTA 使用当前已验证的 4096-byte stream block，build `20260904162319`；
  quick P3 `diagnostic.json` 报告 `flow_completed=true`，TDMA
  `tdma-process-image/summary.json` 报告 `passed=true`、`realtime_gate_passed=true`、
  `closed_loop_passed=true`。原始 P3/OTA/波形证据位于
  `out/hardware-acceptance/sync-la-005-storage-final-20260905/`。
- StorageAO 原始证据：NO1 ARM/STOP 后 `SYSTem:STORage:FILE:WRITe:STATus?` 返回
  `DONE`，size `2072 == 2072`、CRC `2172917391 == 2172917391`，路径为
  `/traces/run/analyzer_00782049.bin`；FILE INFO 与 64-byte READ 均成功，读回 header
  snapshot 为 magic `SLAY`、schema `1`、record_count `64`、dropped_records `37971`、
  payload CRC `2682194261`。上述数值均为本轮板端验收快照。
- 失败保留：早期 CRC 自引用实现的 `sync-la-005-storage-20260904/` 证据显示 StorageAO
  `ABORTED`（expected CRC 与 actual CRC 不一致）；修复为不把 file CRC 字段放入 header 后，
  `sync-la-005-storage-crcfix-20260904/` 已验证 size/CRC 一致。本轮不将 `SYNC-LA-005` 标记
  DONE；active/shadow 完整切换、慢写 drop evidence 和离线 decoder 仍待后续切片。

### SYNC-PROGRESS-20260905-002 - analyzer segment 离线 decoder 首段

- TODO task ID：`SYNC-LA-006`。
- 变更：新增 `tools/analyzer_trace_decode/analyzer_trace_decode.py`，解析 `SLAY` schema 1
  segment，验证文件大小、magic/schema、payload CRC，并可比对 StorageAO 返回的 file CRC；
  输出 record sequence gap/discontinuity、flags、level/edge mask 和可选 hardware tick
  timebase（JSON 或 CSV）。
- 软件验证：`test_analyzer_trace_decode.py`、analyzer contract tests 和 `py_compile` 全部
  通过（本轮 9 tests）。decoder 对 file CRC 的校验不把 CRC 字段写回 header，避免自引用。
- 硬件验收：源码指纹对应 quick P3 build `20260904164736`，四板 4096-byte OTA 成功；
  `tdma-process-image/summary.json` 报告 `passed=true`、`realtime_gate_passed=true`、
  `closed_loop_passed=true`、`cycles=512`。原始证据位于
  `out/hardware-acceptance/sync-la-006-decoder-20260905/`；调试态 coarse CLK 的既有拒绝
  记录按有界继续策略保留，未触发硬停安全类别。
- 边界：本首段只完成 decoder 与元数据基础，不宣称 SVG 波形渲染、profile/source 元数据
  合并或完整 SD 文件批量导出已完成；`SYNC-LA-006` 保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260905-003 - analyzer segment SVG 波形输出

- TODO task ID：`SYNC-LA-006`。
- 变更：`tools/analyzer_trace_decode/analyzer_trace_decode.py` 增加确定性 SVG 输出；按出现的
  GPIO level/edge mask 生成有界 lane，level 用阶梯线、edge 用标记点表达，空数据也输出
  合法占位 lane；`--csv` 与 `--svg` 互斥，保持离线工具不触碰实时固件路径。
- 软件验证：decoder 单测 3 项通过，包含 GPIO lane、edge marker 和 SVG 文件结构断言；
  `py_compile` 通过。
- 硬件验收：源码指纹对应 quick P3 build `20260904170808`，四板 4096-byte OTA 成功；
  `tdma-process-image/summary.json` 报告 `passed=true`、`realtime_gate_passed=true`、
  `closed_loop_passed=true`、`cycles=512`。原始证据位于
  `out/hardware-acceptance/sync-la-006-svg-20260905/`；调试态既有校准拒绝按有界继续策略
  记录，未触发不可恢复硬停。
- 边界：SVG 尚未合并 profile/source 元数据、drop interval 标注和批量 SD 导出索引；
  `SYNC-LA-006` 继续保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260905-004 - analyzer active/shadow 缓冲交接

- TODO task ID：`SYNC-LA-005`、`SYNC-LA-007`。
- 变更：停止态由 Core1 将已完成 capture 以有界记录复制发布到 shadow backing；设备上复用
  `sync_io_shared_workspace`，主 capture ring 保持独立静态缓冲，避免增加固件 BSS。Core0
  仅从 shadow drain，shadow 未排空时 ARM 返回 `BUSY`，排空后下一轮 capture 可复用 active
  backing；发布过程使用 sequence/ready 标志，避免 Core0 读取半发布数据。
- 失败与修复：初版分配第二个完整 record array 导致链接器 RAM overflow `32080 bytes`，
  该失败由 `out/build/sync-la-006-svg-20260905/` 编译输出保留；随后改为 shared-workspace
  shadow，链接恢复通过，未放宽内存门禁。
- 软件验证：analyzer C contract tests 通过；Python analyzer contract tests 通过；固件
  编译、UF2/package 生成和 flash-link checks 通过。
- 硬件验收：四板 4096-byte OTA build `20260904173407`；P3 TDMA
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`、`cycles=512`。
  原始证据位于 `out/hardware-acceptance/sync-la-005-shadow-20260905/`。
- 板端交接证据：NO1 连续两轮 ARM→STOP→StorageAO 均 `DONE`，每轮 size `2072 == 2072`、
  expected/actual CRC 一致，分别记录于 `analyzer-shadow-cycle-1.jsonl` 与
  `analyzer-shadow-cycle-2.jsonl`。本条款不宣称慢写背压/drop evidence 已完成。

### SYNC-PROGRESS-20260905-005 - analyzer segment profile/source/timebase 元数据

- TODO task ID：`SYNC-LA-006`。
- 变更：扩展 SLAY header（保持 schema 1、通过 `header_size` 向后兼容旧 24-byte 文件），
  持久化 source mask、profile/persona generation、hardware tick frequency、timestamp
  resolution 和 capture sequence；decoder 自动解析扩展 metadata，并继续接受旧格式。
- 软件验证：analyzer C contract tests、decoder tests 和固件编译/flash-link checks 通过；
  扩展 header decoder 单测覆盖全部 metadata 字段。
- 硬件验收：四板 4096-byte OTA build `20260904175812`，TDMA
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`、`cycles=512`；
  原始 P3 证据位于 `out/hardware-acceptance/sync-la-006-metadata-20260905/`。
- 板端文件证据：StorageAO 返回 `DONE`，size `2096 == 2096`、CRC
  `3031917687 == 3031917687`；FILE READ 回读 `header_size=48`、source mask `0x3FFF00FF`、
  profile/persona generation `1/1`、tick `1000000000 Hz`、resolution `250000 ns`、
  capture sequence `1`。原始 transcript 为 `analyzer-metadata-arm-stop.jsonl` 和
  `analyzer-metadata-readback.jsonl`。
- 边界：drop interval 标注和完整 SD 批量导出索引仍待后续切片；`SYNC-LA-006` 保持
  `IN PROGRESS`。

### SYNC-PROGRESS-20260905-006 - analyzer 启停前后 TDMA 无扰动 HIL

- TODO task ID：`SYNC-LA-007`。
- 变更：新增 `tools/analyzer_tdma_hil/analyzer_tdma_hil.py`，在单板运行 TDMA ring 上采集
  baseline 窗口，再执行 analyzer ARM/STOP，比较 ring adapter RX、up/down sequence、bad/
  transport/schedule/profile/error 计数；baseline 校准用于区分既有链路噪声和 analyzer 诱发
  回归，所有原始 status 响应写入结果 JSON。
- 软件验证：HIL 逻辑单测和 `py_compile` 通过；首次实测发现既有 bad counter 增长，加入
  baseline 窗口后判据恢复可归因，未放宽错误计数语义。
- 硬件验收：源码指纹对应 quick P3 build `20260904182141`，四板 4096-byte OTA 成功；
  TDMA `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`、`cycles=512`。
  P3 原始证据位于 `out/hardware-acceptance/sync-la-007-hil-20260905/`。
- 专项 HIL：最终板端 `analyzer_tdma_hil-final.json` 报告 `passed=true`；analyzer 窗口
  `ring_adapter_rx_count +652`、up/down sequence `+653/+652`，bad/transport/schedule/profile
  和 last-error 增量均为 `0`；baseline 同类 bad 增量也为 `0`，ring running 保持不变。
- 结论：`SYNC-LA-007` 标记 `DONE`；本证据不替代 NO5/SMA 外部物理波形，`SYNC-LA-008` 仍待
  完成。

### SYNC-PROGRESS-20260904-013 - Core0 bounded analyzer drain boundary

- TODO task ID：`SYNC-LA-005`、`SYNC-LA-002`。
- 变更：新增 `sync_io_logic_analyzer_drain_core0()`；仅当 analyzer persona 已停止/释放且
  hardware backend 不再运行时，Core0 才能按调用方给定容量有界弹出 capture records。活动
  realtime producer、空 capture 或零容量请求均返回 0，不消费 TDMA/目标 FIFO，也不触碰 GPIO。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过；`test_sync_io_logic_analyzer_contract.py`
  通过；self-alias/空 drain 边界由 C contract test 覆盖。
- 构建验证：`out/build/sync-la-002-alias-fix-20260904/` 增量固件重链成功，flash-link contract
  全部通过；默认 `pico2-validation` 的既有 RAM 溢出仍未改变。
- 硬件验收：四板 OTA 完成，build `20260904150214`；TDMA process-image/FIFO 短帧闭环
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`。原始证据位于
  `out/hardware-acceptance/sync-la-005-core0-drain-20260904/`。
- 调试门禁：coarse CLK/TRN-01/TRN-03 的既有诊断异常按有界继续策略记录并继续，未发生
  越界 DMA、非法内存/Flash 或失控 GPIO 硬停；本切片不宣称 StorageAO 持久化已完成。

### SYNC-PROGRESS-20260904-012 - STOP 后 last-capture shadow 复验与 TDMA 闭环

- TODO task ID：`SYNC-LA-005`、`SYNC-LA-002`。
- 变更：复验 `sync_io_logic_analyzer_get_status()` 在活动 persona STOP/release 后回退读取
  Core1 capture shadow；查询保持只读，不重新 claim、消费记录或触碰 PIO/DMA/GPIO。
- 软件验证：`tools/tests/run_sync_io_logic_analyzer_tests.ps1` 通过；
  `test_sync_io_logic_analyzer_contract.py` 与 `test_p3_hardware_acceptance.py` 合计 `37 passed`。
- 构建验证：默认 `pico2-validation` 仍受既有 RAM 溢出阻断；使用已通过布局的
  `out/build/sync-la-002-alias-fix-20260904/` 增量构建成功，三个 flash-link contract 全部通过。
- 硬件验收：四板异步 OTA 完成，build `20260904143914`；P0T、CLK/MARK、TRN-00/01/02/03
  和 TDMA process-image/FIFO 短帧闭环完成。`tdma-process-image/summary.json` 报告
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`；证据目录为
  `out/hardware-acceptance/sync-la-002-last-capture-20260904/`。
- 调试门禁：coarse CLK follower arm、TRN-01 SCK gate 和 TRN-03 安全候选未通过；按调试策略
  保留拒绝原因、状态快照和原始数据并有界继续，未发生越界 DMA、非法内存/Flash 或失控 GPIO
  硬停。`diagnostic.json` 的 `flow_completed=true`、`strict_gates_passed=false`，不倒写为
  analyzer 或校准契约通过。
- 边界：本轮只复验 last-capture shadow 与 TDMA 无扰动；Core0 drain、StorageAO 持久化和
  EDGE/TRIGGERED capture 仍待后续任务。

### SYNC-PROGRESS-20260904-011 - STOP 后保留 analyzer last-capture snapshot

- TODO task ID：`SYNC-LA-005`、`SYNC-LA-002`。
- 变更：`sync_io_logic_analyzer_get_status()` 在活动 persona 已 STOP/release 后，回退读取
  Core1 控制槽内保留的 capture shadow；状态查询继续只读，不重新 claim、消费记录或触碰
  PIO/DMA/GPIO。这样 STOP 后的 end reason、produced/consumed/drop/overrun 和 CRC 仍可追溯。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过；`pico2-validation` 链接阶段受既有
  RAM 溢出（15052 bytes）阻断，使用已通过布局的 `sync-la-002-alias-fix-20260904` 构建目录
  增量重编译通过，重新生成 package/UF2 和 flash-link checks 全部通过。
- 硬件验收：使用同一 package 完成四板 OTA、P0T、CLK/MARK、TRN-00/01/02/03 和
  TDMA `cycles=4096` process-image/FIFO 闭环；`tdma-process-image/summary.json` 报告
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`。统一 P3 的诊断
  capture 仍因既有 SD job timeout 产生 `diagnostic_passed=false`，但未触发 DMA/非法内存/
  Flash/GPIO 硬停；原始失败证据保留在
  `out/hardware_acceptance/sync-la-005-last-capture-20260904/`，因此不把诊断附件失败
  误判为 TDMA 短帧失败，也不提升 `SYNC-LA-005` 状态。
- 同一 package 的第二次完整复跑（`sync-la-005-last-capture-rerun-20260904`）复现相同
  capture/SD timeout，进一步确认当前阻塞位于诊断证据搬运链路，而非 analyzer 状态快照
  读取或 TDMA process-image/FIFO 闭环。

### SYNC-PROGRESS-20260904-010 - Analyzer ARM/STOP 单槽 intent mailbox

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更：新增 `sync_io_logic_analyzer_request_arm/stop()` 和
  `sync_io_logic_analyzer_service_core1()`；Core0 只复制 RAW_SAMPLE 配置并提交一个
  request sequence，Core1 在 TDMA service 之后的 mandatory bounded boundary 消费
  intent、执行 persona
  claim/load/arm/start 或 stop/release，并继续服务有界 record 数。重复请求返回
  `BUSY`，request/handled sequence、命令和结果进入 `STATe?` snapshot。
- SCPI：新增 `REALtime:IO:ANALyzer:ARM`、`...:STOP`；ARM 参数为
  `source_mask,sample_period_ns,max_records,timeout_us,overwrite_oldest`，返回值仅表示
  mailbox accepted，不在 Core0 直接调用 PIO/DMA。新增 `default_source_mask()` 复用
  `LOGIC_ANALYZER` descriptor 的只读 pad mask。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 的 host C contract 通过；
  `test_sync_io_logic_analyzer_contract.py` 为 `6 passed`。统一 P3 已完成 release build、
  package、五板 OTA 和板端流程。
- 边界：本切片仍只支持 RAW_SAMPLE；EDGE/TRIGGERED、Core0 drain、StorageAO 和离线
  decoder 尚未接入，`SYNC-LA-002` 保持 `IN PROGRESS`。
- 板端修正：首轮 HIL 的 ARM 返回 accepted，但 request/handled 为 `1/0`；定位为 service
  原先挂在 optional SYNC_CAPTURE load，可能因 load 隔离而永久不消费。TDMA 环保持运行，
  未触发安全硬停。现已移到 TDMA 优先后的 mandatory Core1 boundary。
- 板端缺陷与修复：mandatory boundary 复验时发现 persona hardware ARM 会用
  `&capture->config` 对同一个 capture 再初始化；旧实现先清零 capture 再复制 config，
  使 RAW_SAMPLE mode 和 overwrite policy 被静默清零并快速 overflow。现先保存已校验配置，
  再清理 runtime 字段，并增加 self-alias reinit host 回归。
- 最终硬件复验：修复包 `build_id=20260904120651` 经五板 OTA、软件复位和完整 P3 编排后，
  四节点 TDMA `cycles=4096` process-image/FIFO 短帧闭环 `passed=true`，startup barrier 和
  每 Node gate 均通过；证据目录为
  `out/hardware_acceptance/sync-la-002-alias-fix-20260904/`。P3 调试模式另保留 coarse CLK 与
  NO5 DPLL 的既有非硬停诊断，`flow_completed=true`、`passed=true`、
  `strict_gates_passed=false`；自动 SD 波形附件当轮不可用，但 TDMA 本体未失败，因此未进入
  波形修复分支。
- NO1 控制面复验：`ARM 0,250000,1024,5000000,1` 后 snapshot 为
  `initialized=1,active=1,state=RUNNING,mode=RAW_SAMPLE,end_reason=NONE`，且
  `request=handled=1,result=ACCEPTED`；STOP 后 manager active/SM/DMA 均归零，
  `request=handled=2,result=ACCEPTED`。同一窗口 TDMA RX 快照从 `33401` 推进到 `33640`，
  `rx_bad` 和 `rx_transport_bad` 均保持不变；原始 transcript 为
  `out/hardware_acceptance/sync-la-002-alias-fix-20260904/analyzer-control-no1.txt`。
- C11 独立交叉审核：由独立审核 agent 按层间交叉（registry ↔ 架构条款 ↔ 代码锚点 ↔
  host/HIL 证据）复核 `ARCH-IOANALYZER-02`，结论 `PASS_WITH_NOTE`。审核确认单槽
  mailbox、mandatory Core1 boundary、TDMA-first 顺序、SCPI intent-only 边界、STOP
  release 和 build/receipt 指纹均一致；注意当前 STOP 后 capture end reason 不持久化，
  后续 `SYNC-LA-005` 再补 last-capture snapshot。
- 后续严格 TDMA 对照（同一固件、无 analyzer 与 analyzer 控制窗口）保留于
  `tdma-baseline-no-analyzer.txt` 与 `tdma-control-preflight/summary.json`。该对照在
  3 秒末端观察到既有 NO1 物理/接收错误计数增长，严格 gate 为 false；流程按规则 STOP
  并保留 SD/原始证据，未将该诊断结果倒写为 analyzer 契约通过或安全硬停。此前完整 P3
  `tdma-process-image/summary.json` 的 4096 闭环仍是本切片的验收凭证，调试模式下的
  coarse CLK/DPLL 诊断继续遵循有界强制继续策略。

## 当前 Checkpoint

### SYNC-PROGRESS-20260904-009 - Read-only analyzer SCPI status snapshot

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更：新增 `sync_io_logic_analyzer_get_status()`，从 analyzer capture 与
  persona-manager 快照组合出只读状态；新增 `REALtime:IO:ANALyzer:STATe?`，返回
  initialized/active、capture state/mode/end reason、produced/consumed/drop/overrun、
  CRC 和 manager 资源/错误字段。查询路径不 ARM、STOP、消费目标 FIFO、修改 GPIO，
  保持 Core0 查询与 Core1 实时 owner 分离。
- 变更：TDMA status 查询遇到持久 CDC `<timeout>` 时执行一次 bounded short-open
  重试；失败仍报告原始 timeout，避免无界重试或伪造状态。
- 软件验证：analyzer host contract、TDMA status recovery、资源与状态机测试通过，
  合计 `49 passed`；host analyzer C contract、`pico2-validation` release build、
  flash/link/SCPI namespace checks 通过。
- 硬件验证：quick diagnostic P3 完成四板 OTA、P0T、CLK/MARK、TRN-00/01/02、
  TRN-03 和四节点 TDMA process-image/FIFO 短帧闭环，结果 `passed=true`；SD 原始
  波形和离线分析保留于
  `out/hardware-acceptance/sync-la-002-scpi-status-20260904-recovered/`。
- 边界：当前 SCPI 仅提供只读状态，尚未提供 analyzer 异步 ARM/STOP 参数解析、
  Core0 ring drain、StorageAO 持久化及 EDGE/TRIGGERED capture；`SYNC-LA-002` 保持
  `IN PROGRESS`。
- 下一步：接入 bounded analyzer ARM/STOP 控制事件，并继续用 TDMA 短帧与 SD 波形
  闭环验证状态机迁移。

### SYNC-PROGRESS-20260904-008 - RAW_SAMPLE persona-manager lease lifecycle

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更：新增 `sync_io_logic_analyzer_persona_begin/end/active/get_snapshot`，将
  `LOGIC_ANALYZER` 的 claim/load/arm/start/stop/release 统一包在现有
  `sync_io_persona_manager` 生命周期中；arm/start/cleanup 失败会回收 PIO program、
  停止 DMA/SM，并保持 capture evidence，不向 Core0/StorageAO 扩散实时操作。
- 资源边界：当前 `INPUT_CAPTURE` 与 analyzer 共用 SYNC_IO 的静态 capture SM/DMA
  保留区；manager 负责逻辑 lease 与 PIO0 arbiter 记录，真实硬件切换仍受
  `sync_io_core_capture_is_running()` 和输出 persona 活跃状态保护，未绕过既有实时
  owner。后续需把 legacy capture 初始化迁移为同一 lease，才能宣称完整动态并发。
- 软件验证：host `run_sync_io_logic_analyzer_tests.ps1` 通过；Python analyzer/资源
  契约测试 `32 passed`；`pico2-validation` release build、flash/link checks 通过。
- 硬件验证：quick diagnostic P3 已重新执行并通过，包含四板 OTA、P0T、CLK/MARK、
  TRN-00/01/02、TRN-03、TDMA process-image/FIFO 短帧闭环；原始 receipt/diagnostic
  位于 `out/hardware-acceptance/sync-la-002-persona-manager-20260904/`，TDMA 波形
  与离线分析随该目录保留。
- 边界：SCPI analyzer 命令、Core0 snapshot/drain、StorageAO 和长期 EDGE/TRIGGERED
  capture 仍未接入，`SYNC-LA-002` 保持 `IN PROGRESS`。
- 下一步：把 `REALtime:IO:SAMPle:*` 维护接口扩展为 analyzer snapshot/arm/stop 查询，
  并在接入后重复 TDMA 短帧与 SD 波形闭环。

### SYNC-PROGRESS-20260904-007 - RAW_SAMPLE hardware backend and bounded transport recovery

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更：`sync_io_logic_analyzer_hw_arm/start/service/stop` 接入只读
  `logic_analyzer_raw_sample` PIO0 backend；DMA 使用已保留的 capture channel 和
  固定共享 SRAM，Core1 service 将完整 pad word 依据已校验 source mask 写入 bounded
  RAW_SAMPLE ring。停止路径先停 SM/DMA，再释放 PIO program，不改变被观测 GPIO
  function/direction，也不执行 SD/USB I/O。
- 变更：拓扑 P0T 工具对持久 CDC status query timeout 增加一次有界 short-open
  readback；原始原因、阶段和恢复结果写入 `transport_recoveries`，失败仍向上报告，
  不把可恢复传输问题伪装成硬件安全拒绝。
- 软件验证：`pico2-validation` release build 通过；相关 Python 契约/资源/拓扑测试
  `39 passed`；`git diff --check` 通过。
- 硬件验证：完整 P3 在 P0T 阶段两次记录单板 CDC timeout（`2BD...`），保留于
  `out/hardware-acceptance/p3-20260904-154434/` 和
  `out/hardware-acceptance/p3-20260904-155041/`；随后使用 SD 原始波形/短帧工具
  复核，四节点 TDMA process-image 闭环 `passed=true`、`left_running=true`，证据位于
  `out/hardware-acceptance/sync-la-002-hw-backend-20260904/tdma-process-image/`。
  采用 quick diagnostic profile 重跑后，P0T、CLK、MARK、TRN-00/01/02、TRN-03 和
  四节点 TDMA 全部通过，receipt/diagnostic 位于
  `out/hardware-acceptance/sync-la-002-hw-backend-20260904/p3-quick-recovered/`。
- 边界：本切片只完成 RAW_SAMPLE 硬件 backend 与有界调试恢复；SCPI analyzer 命令、
  Core0 snapshot/drain、StorageAO 和长期 EDGE/TRIGGERED capture 仍未接入，
  `SYNC-LA-002` 保持 `IN PROGRESS`。
- 下一步：接入 analyzer persona manager 的正式 lease/SCPI arm 查询，并在每次状态
  改动后继续执行 TDMA 短帧和 SD 波形闭环。

### SYNC-PROGRESS-20260904-005 - RAW_SAMPLE bounded record ring

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更：在 `sync_io_logic_analyzer.h/.c` 增加固定 backing storage 的 RAW_SAMPLE ring 原语；
  支持容量上限、顺序 wrap、overwrite-oldest 的 discontinuity/drop/overrun 证据、非覆盖模式
  的 OVERFLOW 终止、CRC32、结束 reason 和 snapshot 导出。路径不使用动态内存、FatFs、USB 或
  目标 FIFO。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过，覆盖 overwrite/drop、overflow stop、
  finish、CRC 和 snapshot；Python 全量回归基线 `705 passed` 仍保持。
- 构建验证：`out/build/pico2-rtos-multicore-smoke` 与 `out/build/pico2-validation` release
  build 通过；package 输出为构建快照，非代码事实源。
- 边界：本记录只完成可复用 bounded capture primitive；尚未把新 ring 接入真实 PIO/DMA
  `LOGIC_ANALYZER` arm，也未宣称 `SYNC-LA-002` 完成。
- 下一步：用最终固件执行统一 P3 与四节点 TDMA 短帧闭环；随后再接入真实 RAW_SAMPLE persona。

### SYNC-PROGRESS-20260904-006 - RAW_SAMPLE read-only PIO program

- TODO task ID：`SYNC-LA-002`、`SYNC-M3`。
- 变更提交：`abcf0bd`，新增 `logic_analyzer_raw_sample` PIO 程序，以单条 `in pins, 32`
  读取完整 pad bus；程序没有 `set pins`、`out pins` 或方向切换，persona catalog 的
  `program_name` 已绑定新身份。
- 软件验证：PIO 静态只读断言、logic analyzer host contract tests 通过；`pico2-validation`
  release build 重新生成 package。
- 硬件验证：统一 QUICK_DIAGNOSTIC P3 证据位于
  `out/hardware-acceptance/sync-la-002-pio-20260904/`；四节点 OTA、TDMA process-image/FIFO
  短帧闭环和 SD 原始 capture/SVG/离线分析完成。coarse CLK follower arm failure 若出现仍
  按调试策略保留为 diagnostic evidence，不改变 TDMA flow completion。
- 边界：本切片只冻结并编译只读 PIO backend；真实 analyzer persona ARM、DMA 绑定、SCPI
  查询和 StorageAO drain 仍属于后续任务，`SYNC-LA-002` 保持 `IN PROGRESS`。

### SYNC-PROGRESS-20260904-004 - 冻结逻辑分析仪公共契约

- TODO task ID：`SYNC-LA-001`、`SYNC-M3`。
- 变更：新增 `sync_io_logic_analyzer.h/.c`，冻结 `RAW_SAMPLE`、`EDGE_TIMESTAMP`、
  `TRIGGERED_CAPTURE` 的 config、record、snapshot、seqlock 读取和强类型结束 reason；source
  mask 直接校验 `LOGIC_ANALYZER` 只读 persona descriptor，配置不含 GPIO 写入字段。
- 门禁策略：新增 gate reason/action。调试模式仅对质量、CRC、超时和资源冲突记录快照及原始
  值，并按 `debug_continue_budget` 有界继续或本轮结束；产品模式严格拒绝；DMA 越界、非法
  内存/Flash 和失控 GPIO 始终硬停。
- 软件验证：`run_sync_io_logic_analyzer_tests.ps1` 通过；状态机资源检查通过；Python 全量
  回归 `705 passed`。VDC domain 既有 host 用例当前独立失败 4 项（slew/acquisition 断言），
  与本切片无文件交集，未将聚合 host 全绿误记为通过。
- 构建验证：`out/build/pico2-rtos-multicore-smoke` 与 `out/build/pico2-validation` release
  build 通过，生成 package build id 为构建快照，非代码事实源。
- 下一步：完成文档门禁后运行统一 P3；因本切片修改固件接口，仍必须复跑四节点 TDMA 短帧闭环，
  通过后再提交并推送。

### SYNC-PROGRESS-20260904-003 - 移除旧 PIO1 suspend/resume handoff

- TODO task ID：`SYNC-RES-003`、`SYNC-M4`。
- 变更提交：`c9c2fd3`，已推送到 `origin/refactor/tdma-phy-split-p3-gated`。TDMA flight
  resource manager 不再调用旧 suspend/resume API；SYNC_IO 不再初始化或持有旧 PIO1 wave
  SM，model pulse 与 fire pulse 统一转入 PIO0 persona 路径，迁移兼容状态位和 trace 已删除。
- 软件/构建验证：`tests/python` 全量验收快照为 `703 passed`；resource-arbiter host tests
  通过；`out/build/pico2-rtos-multicore-smoke` 与 `out/build/pico2-validation` release build
  通过，本轮 package build id 验收快照为 `20260904025945`。
- 硬件验证：统一 QUICK_DIAGNOSTIC P3 证据位于
  `out/hardware-acceptance/sync-res-003-remove-pio1-handoff-20260904/`；四节点使用同一 package
  完成 OTA，每板协商的 `max_data_block_size` 验收快照为 `4096`。`acceptance.json` 的
  `passed=true`、`flow_completed=true`；TDMA `closed_loop_passed=true`、
  `realtime_gate_passed=true`、`left_running=true`，NO1--NO4 SD 原始 capture、SVG 与离线
  分析均保留并通过。
- 诊断与策略：coarse CLK level 7 有一条 follower forwarding arm failure，已记录在
  `acceptance.json.diagnostic_failures`，流程继续完成 TDMA 和波形闭环；无越界 DMA、非法
  内存/Flash 操作或失控 GPIO 硬停条件，因此未拒绝本轮流程。
- 结论：`SYNC-RES-003` 标记为 `DONE`；`SYNC-M4` 继续 `IN PROGRESS`，下一项进入
  `LOGIC_ANALYZER` persona（`SYNC-LA-001`）。

### SYNC-PROGRESS-20260904-002 - PIO0 persona descriptor/lifecycle 与 TDMA 短帧闭环

- TODO task ID：`SYNC-RES-001`、`SYNC-RES-002`、`SYNC-RES-003`、`SYNC-M2`。
- 变更：PIO0 persona descriptor、兼容矩阵和 lifecycle manager 已接入输出/预约触发路径；
  WAVE_OUTPUT 使用 PIO0 专用 SM/DMA/workspace，SCHEDULED_TRIGGER 使用独立 PIO0 SM；
  旧 PIO1 suspend/resume 仍作为兼容层保留，未宣称迁移收尾。
- 软件/构建验证：资源检查通过；状态机/文档定向 pytest 46/46 通过；
  `out/build/pico2-rtos-multicore-smoke` 增量编译通过；4096B OTA 包使用
  `out/build/pico2-validation`。
- 硬件验证：QUICK_DIAGNOSTIC TDMA-only 证据位于
  `out/hardware-acceptance/sync-res-003-pio0-20260904/`。四节点 OTA 成功；
  `tdma-process-image/summary.json` 的 `closed_loop_passed`、`realtime_gate_passed`、
  `left_running` 均为 true；四节点 SD 原始 capture、SVG 和
  `analysis/ring_capture_analysis.json` 均保留且分析通过。NO5 未加入本轮 scope。
- 诊断：coarse CLK level 7 记录一条既有 calibration failure；流程按调试策略继续，未影响
  TDMA 短帧闭环，也未伪造严格 gate 通过。
- 下一步：移除旧 PIO1 suspend/resume compatibility handoff，再进入 LOGIC_ANALYZER persona。

### SYNC-PROGRESS-20260903-001 - SYNC_IO 域与逻辑分析仪契约重构

- TODO task ID：`SYNC-DOC-001`、`SYNC-M1`。
- 日期：2026-09-03。
- 变更：重构 SYNC_IO Architecture/TODO/Task Progress 三件套，纠正旧 PIO0 input、PIO1
  output、PIO2 AUX 分区；将不带限定词的 wave 拆为输出型 `WAVE_OUTPUT` 和只读
  `LOGIC_ANALYZER` persona，并冻结 pad-visible GPIO 旁路观测、目标 GPIO 不接管、目标
  FIFO 不消费、Core1 capture/Core0 落盘和长期 edge timestamp 边界。
- 跨域同步：状态机架构承接 `ARCH-PIOPARTITION-01`，SYNC_IO 架构承接
  `ARCH-IOANALYZER-01`；两项登记保持 pending，等待独立交叉审核后才能变更状态。
- 代码边界：本记录只重构文档，不修改固件、PIO、工具或测试，不产生新的 OTA/HIL 结论；
  当前 `BOARD_SYNC_PIO_WAVE` 到 TDMA TX PIO 的 suspend/resume handoff 仍是待迁移实现。
- 验证：`docs_check.py --strict-names` 通过，验收快照（非事实源）为 `files=121,
  warnings=0`；`doc_regression_check.py` 通过，登记表验收快照（非事实源）为 `25 contracts`，
  只保留既有旧格式 `TDMA-FLIGHT-BITMAP-01` warning；文档 pytest 验收快照（非事实源）为
  `18 passed`；pre-commit 通过并正确跳过纯文档变更的硬件门禁；`--log-check` 与
  `p3_hardware_acceptance.py check-staged` 均通过。
- 下一步：先完成 PIO0 persona descriptor 和兼容矩阵，再迁移输出 persona；逻辑分析仪按
  RAW、EDGE、TRIGGERED、Core0/SD 的顺序实现。

## 验证与证据索引

| progress ID | TODO task ID | 证据 |
|---|---|---|
| SYNC-PROGRESS-20260903-001 | SYNC-DOC-001 / SYNC-M1 | 本次文档 diff；docs_check、doc_regression、文档 pytest、pre-commit、log-check 和 staged acceptance 均通过。 |

## 历史实施记录

### SYNC-PROGRESS-20260816-008 - TDMA window capture budget

- TODO task ID：`SYNC-VDC-001`。

- 状态：代码、构建和 COM5 单板验证完成；COM6 物理恢复后待两板 HIL。
- 目标：解决 10 MHz capture self-test 中 core1 无界消费 DMA ring，避免窗口外采样拖垮 realtime loop 和 USB/SCPI。
- 完成：
  - `sync_io` capture 使用 DMA channel 3 环形缓存，core1 每次最多处理 128 个 word。
  - capture timestamp window 已 arm 时，依据硬件 tick timebase 和 DMA word sequence 跳过窗口外 backlog，只把 observation window 内 raw word 写入 latch ring。
  - 窗口外跳过不计为 DMA overflow；真实 ring 溢出仍清除 timebase valid 并保留 drop evidence，不能伪造 `DPLL_ELIGIBLE`。
  - VDC observer 单次批量从 8 提升到 32，覆盖一个 10 us / 100 ns observation window 的完整 word 集。
- 验证：
  - `cmake --build build-rtos-multicore-smoke` 通过，最终 build `20260816065050`，package CRC `0x65948FE3`。
  - 17/17 host unit test scripts、VDC tests、SCPI dry-run、docs check 通过。
  - COM5 已 OTA/boot/commit 到 `20260816065050`，`SYST:LOOP:STAT?` 持续增长，TX-only PIO/DMA self-test 成功。
  - COM6 在旧版高负载 self-test 后 CDC 不响应；OTA 和 picotool 强制 BOOTSEL 均无法完成，需物理复位或重新插拔后再部署最终包。
- 后续：
  - COM6 恢复后使用 `tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 完成 X->Y、Y->X 双向验证；必须看到 accepted sample、gate pass、hardware tick、`DPLL_ELIGIBLE` 且无 `DIAGNOSTIC_ONLY`。

### SYNC-PROGRESS-20260816-007 - timer1 hardware tick diagnostic latch

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 目标：把上一阶段 `time_us_64()*1000` 软件微秒 latch 升级为本地硬件 tick 时间基，同时不把 core1 drain FIFO 时刻冒充为 PIO 边沿硬锁存。
- 完成：
  - `sync_io` 初始化 `timer1_hw`，source 选 `CLK_SYS`，保留 SDK/default `timer0`。
  - `sync_io_capture_latch_service_core1()` 在 core1 搬运 PIO capture FIFO 时读取 `timer1` raw tick，并按 `clk_sys` 转换为 ns。
  - `REALtime:IO:SAMPle:LATCh?` 返回字段扩展为 9 项：`initialized,capture_running,capture_sample_hz,dropped_capture_words,latched_capture_words,dropped_latched_capture_words,capture_latch_source,capture_latch_resolution_ns,capture_latch_flags`。
  - VDC compact observation 保留 latch fact 自己声明的 timestamp source/resolution/flags；dictionary 只做 event/source slot/reference slot/payload class 语义匹配，不覆盖采样事实。
  - VDC gate 拒绝 source 与 dictionary 不一致的 compact sample，并拒绝带 `DIAGNOSTIC_ONLY` 的 hardware tick sample 进入 DPLL lock gate。
- 边界：
  - 本阶段报告 `HARDWARE_TICK / <=100 ns / DIAGNOSTIC_ONLY`。
  - 时间戳仍发生在 core1 drain FIFO 时刻，不等同于 PIO 边沿硬件锁存；只能作为 TDMA/VDC bring-up 诊断证据。
  - 后续仍需 PIO/DMA/IRQ/core1 edge latch，才允许生成 `DPLL_ELIGIBLE` observation sample。
- 验证：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，generated=66。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816040434`，OTA package CRC `0x84D58EA6`。
  - COM5/COM6 均 OTA 到 build `20260816040434` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `python tools\vdc_latch_validate\vdc_latch_validate.py COM5 COM6 --expected-build 20260816040434 --out-dir build-rtos-multicore-smoke\vdc_latch_validate_20260816040434` 通过：COM5 `latched=118->270, observer_raw=0->274, source=2, resolution_ns=4, flags=1`，COM6 `latched=118->270, observer_raw=0->276, source=2, resolution_ns=4, flags=1`。
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816040434 --out-dir build-rtos-multicore-smoke\vdc_observer_validate_20260816040434` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
  - 后续 gate 增强验证见 `VDC-TASK-20260816-023`：COM5/COM6 在 build `20260816042527` 上 forced edge 均进入 observer，且 `submitted=1,accepted=0,rejected=1,gate=9`，证明 `DIAGNOSTIC_ONLY` hardware tick 没有进入 DPLL accepted path。

### SYNC-PROGRESS-20260816-006 - core1 capture latch phase 1

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 目标：把 PIO capture FIFO 从 core0 轮询 raw word，推进为 core1 realtime 侧搬运的 timestamped capture fact，供 VDC observer 消费。
- 完成：`sync_io_capture_latch_service_core1()` 在 core1 realtime loop 中从 `sync_capture_4bit` RX FIFO 读取 raw word，写入 bounded latch ring；每个 word 附带 `sample_seq`、`base_time_l32_ns`、`sample_period_ns`、timestamp source/resolution/flags 和 drop counter。
- 完成：`vdc_dpll_manager` 改为读取 `sync_io_read_capture_latched()`，不再直接消费 PIO FIFO；dictionary 只补 source/reference/payload 语义，不覆盖 latch 自身声明的 timestamp source/resolution/flags。
- 完成：新增 `REALtime:IO:SAMPle:LATCh?`，返回 `initialized,capture_running,capture_sample_hz,dropped_capture_words,latched_capture_words,dropped_latched_capture_words,capture_latch_source,capture_latch_resolution_ns`。
- 边界：本阶段 timestamp 仍来自 `time_us_64()*1000`，固定报告 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`；它只能作为 bring-up 诊断，不允许进入 100 ns DPLL lock gate。
- 验证：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 生成 66 条，包含 `REALtime:IO:SAMPle:LATCh?`。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816034347`，OTA package CRC `0x962F5B65`。
  - COM5/COM6 均 OTA 到 build `20260816034347` 并 commit；`SYSTem:ERRor?` 均为 `0,"No error"`。
  - `python tools\vdc_latch_validate\vdc_latch_validate.py COM5 COM6 --expected-build 20260816034347 --out-dir build-rtos-multicore-smoke\vdc_latch_validate_20260816034347_v2` 通过：COM5 `latched=117->270, observer_raw=31->307`，COM6 `latched=120->272, observer_raw=31->310`。
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816034347 --out-dir build-rtos-multicore-smoke\vdc_observer_validate_20260816034347` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 后续：先升级为 `timer1/CLK_SYS` hardware tick diagnostic latch，再继续推进 PIO/DMA/IRQ/core1 edge latch，最终形成 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE` 的 VDC observation sample。

### SYNC-PROGRESS-20260816-002 - VDC raw capture observer 接线

- TODO task ID：`SYNC-VDC-001`。

- 目标：把 `sync_io_read_capture_words()` 产出的 raw IO fact 接到 VDC compact observation path，同时保持 SYNC_IO 不解释 TDMA/DPLL 语义。
- 完成：`vdc_dpll_manager` 增加默认关闭的 sync_io observer 配置和状态 API；启用后每次 VDC service 读取 bounded raw word batch，经 `VdcSyncIoAdapter` 转为 `VdcCompactObservationSample`，再提交 VDC dictionary/wrap/gate。
- 完成：observer 状态记录 raw word、capture result、sample seq、event id、tick_l32、accepted/rejected/gate reject 计数；不自动启动 capture，不用 `board_uptime_ms()` 伪造硬件 timestamp。
- 完成：新增 `SYSTem:SYNC:VDC:OBServer?` 只读维护查询，返回 observer enabled、batch、raw/no-edge/ambiguous/submitted/accepted/rejected、last raw word、event id、tick_l32 和 gate reject；查询不触发 capture 或样本提交。
- 验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
- 验证：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
- 验证：`python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=127。
- 验证：`python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 验证：`git diff --check` 通过，仅有既有 CRLF 提示。
- 验证：`cmake --build build-rtos-multicore-smoke` 通过，最新生成 build id `20260816024745`，package CRC `0x028BC853`。
- 验证：COM5/COM6 均 OTA 到 build `20260816024745` 并 commit；两板 `SYST:SYNC:VDC:OBServer?` 均返回 18 个零字段，符合默认 disabled observer；`SYST:ERR?` 均为 `0,"No error"`。

### SYNC-PROGRESS-20260816-003 - VDC raw capture observer 维护配置

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、文档、build 和 COM5/COM6 启停态 HIL。
- 完成：新增 `SYSTem:SYNC:VDC:OBServer` 维护配置命令；无参数或 `0` 只关闭 observer 并清零状态，避免产品枚举或误调用时打开采集链路。
- 完成：启用态要求显式给出 batch、rising/falling event id、observed mask、initial mask、base tick、sample period、expected window start 和 frame CRC，随后交由 `vdc_dpll_manager_configure_sync_io_observer()` 做合法性检查。
- 边界：该命令只配置 VDC manager observer，不启动 `sync_io` capture，不直接写 DPLL，不把 raw word 当作 lock evidence。
- 验证：
  - build `20260816030427`，OTA package CRC `0x5D46BBBD`。
  - COM5/COM6 均 OTA boot/commit 成功，`SYSTem:FW:BUILD?` 返回 `"20260816030427"`，`SYSTem:ERRor?` 返回 `0,"No error"`。
  - COM5/COM6 执行无参数 `SYST:SYNC:VDC:OBServer` 均返回 `1`，查询返回 disabled 全零字段。
  - COM5/COM6 执行 `SYST:SYNC:VDC:OBServer 1,1,1,2,1,0,0,1000,0,0,1` 均返回 `1`，查询显示 `enabled=1,max_words_per_service=1`；随后 `SYST:SYNC:VDC:OBServer 0` 关闭后查询回到 disabled 全零字段。

### SYNC-PROGRESS-20260816-004 - VDC observer HIL evidence fields

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成代码、文档、build 和 COM5/COM6 启停态字段验证。
- 完成：`SYSTem:SYNC:VDC:OBServer?` 在原 18 字段之后追加配置和证据字段：rising/falling event、observed mask、initial mask、sample period、expected window、frame CRC、schedule CRC、dictionary CRC/profile CRC、edge index、timestamp source/resolution/flags、source/reference slot 和 payload class。
- 边界：关闭态仍保持 40 字段全零；启用态只暴露 observer 配置和 VDC dictionary/profile 证据，不启动 capture，不把空 FIFO 或软件时间当作 DPLL lock。
- 验证：
  - build `20260816031400`，OTA package CRC `0x0F669557`。
  - COM5/COM6 均 OTA 到 build `20260816031400`；`ota_boot_commit.py` 两次均因启动日志污染误判失败，独立 `scpi_query` 确认 `SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`、`SYST:OTA:SLOT? -> 2,0,2,0,0`、`SYST:ERR? -> 0,"No error"`。
  - COM5/COM6 启用最小合法 observer 后，`OBServer?` 返回 40 字段，其中 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`、`dictionary_profile_crc32=974530568`；关闭后返回 40 个零字段。

### SYNC-PROGRESS-20260816-005 - VDC observer HIL script 固化

- TODO task ID：`SYNC-VDC-001`。

- 状态：完成并通过 COM5/COM6。
- 完成：新增 `tools/vdc_observer_validate/vdc_observer_validate.py`，固化 observer disable/enable/query/disable/error 验证流程，避免后续继续手写串口命令。
- 验证：`python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816031400` 通过，两板均确认 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 风险：当前仍依赖显式配置的 `next_base_time_l32_ns`、`sample_period_ns`、event id 和 frame CRC；真实 PIO/DMA timestamp latch 和 COM5/COM6 HIL 证据待后续补齐。
- 后续：补 HIL，把 dictionary CRC、edge index、timestamp source/resolution、profile CRC 和 VDC gate result 写入报告。
- 涉及文件：`components/vdc_dpll_manager/inc/vdc_dpll_manager.h`，`components/vdc_dpll_manager/src/vdc_dpll_manager.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260816-001 - 通用 IO 观测器归属纠偏

- TODO task ID：`SYNC-LA-001`。

- 目标：明确 `sync_io_read_capture_words()` 的架构归属，避免把通用 raw IO capture primitive 误写成 VDC/TDMA 私有能力。
- 完成：`SYNC_IO_ARCHITECTURE.md` 增加“通用 IO 观测器”章节，说明该接口读取 `sync_capture_4bit` PIO RX FIFO，每个 32-bit word 表示 8 个连续 4-bit 输入采样。
- 完成：明确该接口可观测 TDMA 通讯/同步 PIO 线、转台输入脉冲、READY/GATE/ARM/AUX 状态和调试线序，但只提供 raw IO fact，不解释业务语义，不写 DPLL/Trigger/RefMem/VDC。
- 完成：`SYNC_IO_TODO.md` P5 增加 raw observation 到 VDC adapter、转台输入/脉冲计数解释路径的后续接线项。
- 验证：`python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 风险：当前只是文档归属纠偏；代码中 `sync_io_read_capture_words()` 到 `VdcSyncIoAdapter`、转台输入 AO/FB 的实际任务接线仍未完成。
- 后续：实现 `sync_io` raw capture word 到 VDC compact observation 的任务接线，并为转台输入/脉冲计数建立独立解释路径，避免复用 VDC 语义。

### SYNC-PROGRESS-20260815-001 - 建立 IO 三分标准文档

- TODO task ID：`SYNC-DOC-001`。

- 目标：按 HAOFV 架构把 `docs/sync` 的 IO 文档收敛为架构、待办、任务进度三份标准入口，避免资源规划、重构计划和评审待办继续分叉。
- 完成：新增 `SYNC_IO_ARCHITECTURE.md`，明确 `SYNC:*`、`REALtime:*`、`sync_io`、mode driver、PIO/DMA/IRQ、RefMem 和 VDC 的 owner 边界。
- 完成：新增 `SYNC_IO_TODO.md`，把当前优先级收敛到 PIO 预约输出、真实最小物理链路、AUX 语义通道、mode self-test、组件化和 RefMem/VDC 集成。
- 完成：更新 `README.md`，将 `SYNC_IO_ARCHITECTURE.md`、`SYNC_IO_TODO.md`、`SYNC_IO_TASK_PROGRESS.md` 设为当前三分 canonical，旧资源规划和重构评审文档降为支撑输入。
- 风险：本次只做文档结构纠偏，未改代码；`ModelTurntableAO` 的 PIO 预约输出和真实 transport 仍是下一步实现重点。
- 后续：按 `SYNC_IO_TODO.md` P0 开始实现模型转台 PIO 预约输出路径，代码修改必须保持 AO 生成计划、`sync_io` owner 装载硬件、PIO/DMA 执行边沿的边界。

### SYNC-PROGRESS-20260815-002 - 融合旧 SYNC 文件并准备删除

- TODO task ID：`SYNC-DOC-001`。

- 目标：旧 `SYNC_IO_RESOURCE_PLAN.md`、`SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_ARCH_REVIEW_TODO.md` 和 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 已落后当前 HAOFV/RefMem/VDC 主线，先吸收有效内容，再删除旧入口。
- 完成：`SYNC_IO_ARCHITECTURE.md` 增加 PIO/DMA 资源基线、mode 资源互斥、CAL_RING / 分布式同步链路边界和预约触发链路。
- 完成：`SYNC_IO_TODO.md` 的文档清理项标记旧文件有效内容已迁入新三分结构，剩余动作是删除旧文件并修正全仓引用。
- 风险：历史任务记录中仍会提到旧文件名作为当时变更对象；这类记录只表达历史上下文，不再作为当前 canonical 入口。
- 后续：删除旧文件，更新 README、arch、trigger、vdc、hardware、communication 等引用到 `SYNC_IO_ARCHITECTURE.md` / `SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-001 - TriggerFB RESET/FAULT 语义收敛

- TODO task ID：`SYNC-MODE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-00，统一 `TRIG_EVENT_RESET` 在所有 ECC 状态下的语义，并为 reset/fault/clear/disarm 后的资源释放补充低频 trace。
- 完成：新增 `fb_reset_all()`，统一停止 clock/capture、释放 SEQ/ENC/BISS owner、回到 `TRIG_STATE_IDLE`、清理 `error_code` 和 `active_mode`；所有 ECC 表中的 `TRIG_EVENT_RESET` 均改为 `fb_reset_all()`。
- 完成：`TRIG_EVENT_CLEAR_FAULT` 保持复用 `fb_release_running_io()`，并补齐 `active_mode` 归零；`TRIG_EVENT_FAULT` 继续复用同一释放 helper 后进入 `TRIG_STATE_FAULT`。
- 完成：TriggerAO 新增 `trigger.resource_release` trace，字段为 `trigger_event`、`before_state`、释放前 `active_resources` 和 `released_resources`；离线解码器已同步解析事件 45。
- 完成：`tools/sd_board_validate/sd_board_validate.py` 增加 `--validate-trigger-release`，按板端路径执行 SEQ_STEP ARM 后 `*RST`、再次 ARM 后 `TRIG:FAULT`，并要求 fault trace 中同时出现 RESET/FAULT 的资源释放记录。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py` 通过。
- 验证：`picotool load -f -v -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录和 Flash verify 通过，设备重启后 COM5 `*IDN?` 正常返回。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_00_release` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --out-dir build-codex-sync-refactor\sd_validation_default_after_release_tool` 通过，确认新增 release-path 选项未破坏默认 SD/trace 验证。
- 验证：P0-01 合入后的最终 build 上复跑 `python tools\sd_board_validate\sd_board_validate.py COM5 --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_00_release_final` 通过。
- 验证：`decoded_fault_trace.json` 中 trace header/size/CRC/idx 全部通过；`trigger.resource_release` 记录显示 RESET 和 FAULT 均从 `SEQ_ARMED` 释放 `PIO1`、`DMA`。
- 风险：P0-00 已完成 SEQ_STEP 板端释放闭环；ENC/BISS 的 RESET/FAULT 路径共用同一 `fb_reset_all()` 和 `fb_release_running_io()`，后续在 P0-01/P0-03 做 BiSS 专项板端验证时继续覆盖。
- 后续：进入 P0-01 BiSS runtime timeout/sample scan 闭环，将 sample scan 步进从 helper 静默 re-arm 收敛到 TriggerFB 管理面 action。
- 涉及文件：`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-002 - BiSS timeout/sample scan 闭环

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-01，让 BiSS runtime timeout/sample scan 可持续推进、re-arm 失败可进入稳定错误路径，并能由板端工具验证 trace 证据。
- 完成：`biss_node_io_poll_runtime()` 返回结构化 poll 结果；timeout scan 只准备下一步 delay 和 TAP config，不再在 helper 内静默 re-arm。
- 完成：TriggerFB 在 `BISS_ARMED` 的 runtime action 中处理 `BISS_NODE_IO_POLL_SCAN_STEP`，通过 mode ops 执行 BiSS TAP re-arm；失败时进入 `TRIG_STATE_FAULT` 并设置 `TRIG_ERROR_IO_ARM_FAILED`。
- 完成：scan re-arm 成功后由 TriggerFB 回写 `biss_node_io_sample_scan_rearm_succeeded()`，解除 timeout latch 并更新时间戳；非 scan 模式保持单次 timeout latch。
- 完成：TriggerAO 新增 `trigger.biss_timeout` 和 `trigger.biss_scan_step` trace；`sd_trace_decode.py` 已解码 timeout count、sample delay 前后值、scan index 和 wrap count。
- 完成：`tools/biss_board_validate/biss_board_validate.py` 增加 `--scan-wait-s`、`--expect-scan-steps` 和 `--capture-trace`，可等待 scan 推进、触发 fault evidence、回读 trace 并断言 BiSS timeout/scan-step 事件。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707155208` 的 factory/update 产物。
- 验证：`picotool load -f -v -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录和 Flash verify 通过，COM5 `SYST:FW:BUILD?` 返回 `"20260707155208"`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --enable-scan --skip-inject --timeout-us 1000 --sample-delay 4 --scan-start 4 --scan-end 12 --scan-step 2 --scan-wait-s 5 --expect-scan-steps 2 --capture-trace --out-dir build-codex-sync-refactor\biss_validation_p0_01_scan` 通过。
- 验证：`decoded_fault_trace.json` 中 trace header/size/CRC/idx 全部通过；`trigger.biss_scan_step` 记录显示 sample delay 按 4→6→8→10→12 推进，`sample_scan_index` 到 4。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_01_default` 通过，确认普通 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 未被 sample-scan 改动破坏。
- 风险：P0-01 已覆盖无帧 timeout scan 的板端路径；re-arm 失败路径通过代码路径和 trace/error 逻辑闭合，后续 P0-03 拆物理 arm 边界时应补一个故意非法 mode config 的 fault 注入验证。
- 后续：进入 P0-02 resource owner 边界，避免后续移动 mode arm 时引入重复 acquire。
- 涉及文件：`components/sync_trigger/inc/biss_node_io.h`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/biss_board_validate/biss_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260707-003 - Resource owner 边界收口

- TODO task ID：`SYNC-RES-001`、`SYNC-MODE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-02，保持 TriggerFB 为唯一资源 owner，并让资源申请/释放与 mode ops 的 `.resources` 表字段一致。
- 完成：新增 `trigger_resource_map` 适配层，在 Trigger 域内将 `sync_io_mode_ops_t.resources` 映射到 `resource_arbiter_resource_t`，避免 `sync_io` core 反向依赖系统仲裁器。
- 完成：TriggerFB 的 SEQ/ENC/BISS acquire/release 全部改为从 mode ops 表驱动获取资源；裸 `sync_io_*_arm()` 仍不直接 acquire，避免多层重复持有。
- 完成：TriggerAO 的 resource snapshot trace 改用同一映射入口；资源冲突仍由 FB action 设置 `TRIG_ERROR_RESOURCE_CONFLICT` 并触发既有 `trigger.resource_snapshot`。
- 完成：`tools/sd_board_validate/sd_board_validate.py` 增加 `--validate-resource-owner`，按板端路径验证 SEQ、ENC、BISS arm 后 `SYST:RES?` 包含预期资源，disarm 后资源释放。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707160600` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_02_resource_owner` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_02_default` 通过，确认 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 未被资源映射收口破坏。
- 风险：ENC_COUNT 现在按 mode table 映射为 `PIO1|DMA`，比旧手写 mask 多仲裁 DMA；这与 ENC 物理实现实际使用 DMA 的事实一致，但会改变未来与其他 DMA mode 的冲突判定。
- 后续：进入 P0-03 BiSS TAP 物理 ARM 边界，继续保持 TriggerFB owner 和 mode driver 物理实现边界清晰。
- 涉及文件：`components/sync_trigger/inc/trigger_resource_map.h`，`components/sync_trigger/src/trigger_resource_map.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-004 - BiSS TAP 物理 ARM 边界收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-03，让 BiSS TAP 的物理 PIO arm/disarm/is_running/read FIFO 实现归属 `sync_io_mode_biss_tap.c`，TriggerFB 继续只负责 ECC、资源 owner 和错误码。
- 完成：新增 `sync_io_core_internal.h`，只暴露 mode driver 所需的 core 初始化状态、PIO program offset、AUX 模式标记和 trace helper；没有暴露 `sync_io_context_t`。
- 完成：`sync_io_biss_tap_arm()`、`sync_io_biss_tap_disarm()`、`sync_io_biss_tap_is_running()`、`sync_io_biss_tap_read_frame_word()` 的物理实现从 `sync_io.c` 搬到 `sync_io_mode_biss_tap.c`。
- 完成：`sync_io_mode_biss_tap.c` 增加 mode 级 disarm/is_running/read/rx_fifo_full API，ops 表的 disarm/is_running 指向 mode driver；`biss_node_io` 不再直接包含 PIO/board SM 细节。
- 完成：硬件 pinout、AUX 方向和物理 frame/sample edge 限制保留在 `sync_io_biss_tap_mode_validate()`；协议 profile 范围、CRC/status/sample scan 语义继续由 `biss_profile_validate()` 和 `biss_node_io` 管理。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707161717` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_03_default` 通过，确认 BiSS TAP 配置、ARM、软件 frame crossing、DISARM 正常。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --enable-scan --skip-inject --timeout-us 1000 --sample-delay 4 --scan-start 4 --scan-end 12 --scan-step 2 --scan-wait-s 5 --expect-scan-steps 2 --capture-trace --out-dir build-codex-sync-refactor\biss_validation_p0_03_scan` 通过，确认 timeout sample-scan re-arm 和 trace 解码正常。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_03_resource_owner` 通过，确认 TriggerFB owner/release 边界未回退。
- 风险：为兼容现有调用，`sync_io.h` 中的 `sync_io_biss_tap_*` API 仍保留，但实现已位于 mode driver；P0-04 拆 `sync_io.c` 时可继续评估是否把这些声明迁入 mode 专用头。
- 后续：进入 P0-04 `sync_io.c` 单体拆分，优先按 mode driver 边界搬迁 SEQ_STEP 和 ENC_COUNT。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/inc/sync_io_mode_biss_tap.h`，`components/sync_io/src/sync_io_mode_biss_tap.c`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-005 - SEQ_STEP 物理实现搬迁

- TODO task ID：`SYNC-MODE-001`、`SYNC-COMP-001`。

- 目标：推进 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-04，将 SEQ_STEP 的物理 PIO/DMA/IRQ 实现从 `sync_io.c` 搬到 `sync_io_mode_seq_step.c`，但不在本子步骤搬迁 ENC_COUNT。
- 完成：`sync_io_seq_step_t` 状态、`sync_io_seq_step_arm()`、`sync_io_seq_step_disarm()`、index/rollover/runtime/trace 采样和 SEQ DMA IRQ service 已搬迁到 `sync_io_mode_seq_step.c`。
- 完成：`sync_io.c` 保留共享 `sync_io_core_dma_irq_handler()`，由它清 DMA IRQ 后分派到 SEQ mode 的 `sync_io_seq_step_dma_irq_service()` 和现有 ENC_COUNT IRQ service，避免本次改动同时迁移 ENC。
- 完成：`sync_io_core_internal.h` 扩展 SEQ/ENC 共享 DMA IRQ 常量、runtime flag/PIO state 打包 helper 和 SM enabled 查询；mode driver 不直接访问 `sync_io_context_t`。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707162734` 的 factory/update 产物。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_04_seq_owner` 通过，覆盖 SEQ ARM/DISARM、RESET/FAULT release 和 fault trace decode。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_04_seq_regression` 通过，确认本次共享 core helper/IRQ 分派调整未回退 BiSS TAP。
- 风险：共享 DMA IRQ 仍服务 SEQ_STEP 和 ENC_COUNT；运行互斥由 TriggerFB/resource owner 保证。ENC_COUNT 尚在 `sync_io.c`，下一步迁移时应把 ENC IRQ service 一并搬到 `sync_io_mode_enc_count.c`。
- 后续：继续 P0-04 的 ENC_COUNT 物理实现搬迁，并复跑资源 owner 中的 ENC ARM/DISARM 板端断言。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_seq_step.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-006 - ENC_COUNT 物理实现搬迁与 P0-04 收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-COMP-001`。

- 目标：完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P0-04，将 ENC_COUNT 的物理 PIO/DMA/IRQ 实现从 `sync_io.c` 搬到 `sync_io_mode_enc_count.c`，并保持 TriggerFB 作为唯一资源 owner 边界。
- 完成：`sync_io_enc_count_t` 状态、`sync_io_enc_count_arm()`、`sync_io_enc_count_disarm()`、count/runtime/trace 采样和 ENC DMA IRQ service 已搬迁到 `sync_io_mode_enc_count.c`。
- 完成：`sync_io_core_dma_irq_handler()` 保留在 `sync_io.c`，只负责清 `DMA_IRQ_0` 中断位并分发到 `sync_io_seq_step_dma_irq_service()` 和 `sync_io_enc_count_dma_irq_service()`。
- 完成：`sync_io.c` 不再持有 BiSS TAP、SEQ_STEP、ENC_COUNT 的物理 mode arm/disarm 实现；当前约 772 行，保留 core 初始化、capture、clock、pulse、AUX、trace helper、`sync_io_context_t` 和共享 IRQ 分发。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707163440` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p0_04_enc_owner` 通过，覆盖 SEQ/ENC/BISS owner、DISARM 释放和 RESET/FAULT release trace。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p0_04_enc_regression` 通过，确认共享 IRQ 分发调整未回退 BiSS TAP 默认闭环。
- 风险：SEQ_STEP 和 ENC_COUNT 仍共享 `BOARD_SYNC_OUTPUT_SM`、`DMA_IRQ_0`，运行互斥依赖 TriggerFB/resource owner；该共享关系已作为 P1-01 后续显式资源表/断言任务保留。
- 后续：进入 P1-01，将 PIO instance、SM、DMA channel、IRQ 的互斥关系显式记录到 mode resource 表或验证表，避免后续并发 mode 改动误用共享资源。
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_enc_count.c`，`docs/sync/SYNC_IO_TODO.md`。

### SYNC-PROGRESS-20260708-007 - SYNC_IO P1 架构小项收口

- TODO task ID：`SYNC-RES-001`、`SYNC-MODE-001`。

- 目标：按 HAOFV 分层完成 `SYNC_IO_ARCH_REVIEW_TODO.md` P1-01 到 P1-05，保持 TriggerFB 为唯一 owner 边界，不把资源 acquire 下沉到 mode driver。
- 完成：`sync_io_mode_ops_t` 增加 `hw` 元数据，记录 PIO instance、SM mask、DMA channel mask 和 IRQ mask；SEQ_STEP/ENC_COUNT 显式声明共享 `pio1/sm0` 和 `DMA_IRQ_0`，BiSS TAP 声明 `pio2/sm0,2,3`。
- 完成：`trigger_resource_map` 从 mode `.resources` 和 `.hw` 共同派生 `resource_arbiter` mask；`sync_io_core_dma_irq_handler()` 增加 SEQ/ENC 不能同时运行的 ISR 入口断言。
- 完成：RJ45 trigger 按硬件层语义增加 `BOARD_SYNC_RJ45_TRIG_IN_PIN`、`BOARD_SYNC_RJ45_TRIG_OUT_PIN` 和 `BOARD_SYNC_RJ45_TRIGGER_SM`；硬件定义优先，历史 `MARK:*` 兼容命令输出到 `RJ45_TRIG_OUT`，不再定义独立 marker 物理信号。
- 完成：`SYNC_IO_MODE_VOID_DISPATCH()` 宏替代三个 mode wrapper 中重复的 `const void*` 转 typed config 胶水函数。
- 完成：预留 mode (`AUX_DIFF_TRIGGER`、`SELF_CAL`) 在 `sync_io_mode_get_ops()` 中显式返回 NULL，`sync_io_mode_get_by_index()` 只枚举已实现 mode。
- 完成：`TRIG_MODE_BISS_BRIDGE` 保留为 deprecated 兼容别名；真实语义使用 `TRIG_MODE_PROTOCOL_TRIGGER + protocol + biss_role`，Bridge 是 BiSS role 子角色。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707164537` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool load -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录通过；`picotool verify -f build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` Flash verify 通过。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_p1_sync_arch` 通过，覆盖 mode resource map、SEQ/ENC/BISS owner 和 RESET/FAULT release。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_p1_sync_arch` 通过，确认 BiSS TAP 和 RJ45 trigger 语义入口未回退。
- 风险：历史 ABI 中仍保留 `MARK:*` / `marker_width_us` 名称；这些名称只表示 RJ45 trigger 兼容入口，不表示独立硬件输出。
- 后续：如需继续清理，可在 SCPI/UI 层新增正式 `RJ45:*` 命令，再把 `MARK:*` 标记为 deprecated 兼容命令。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/inc/sync_io_mode.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_*.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_resource_map.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`。

### SYNC-PROGRESS-20260708-008 - RJ45_TRIG 硬件定义优先收口

- TODO task ID：`SYNC-PROFILE-001`。

- 目标：按硬件定义优先原则，舍弃独立 `MARKER_OUT` 物理信号，将历史 `MARK:*` 命令收敛为 `RJ45_TRIG_OUT` 兼容入口，避免 `pio1/sm3` 误驱动 AUX3/GPIO29。
- 完成：`BOARD_SYNC_MARKER_OUT_PIN` 改为 deprecated alias，指向 `BOARD_SYNC_RJ45_TRIG_OUT_PIN`；`BOARD_SYNC_AUX3_OUT_PIN` 只表示 AUX3 固定输出，不再承载 marker 语义。
- 完成：`sync_io_init()` 使用 `BOARD_SYNC_RJ45_TRIGGER_SM` + `BOARD_SYNC_RJ45_TRIG_OUT_PIN` 初始化 `pio1/sm3`；旧 `sync_io_fire_marker_*()` 保留为 RJ45 trigger 兼容函数。
- 完成：TriggerFB 的 `TRIG_EVENT_FIRE_MARKER` 直接调用 `sync_io_fire_rj45_trigger_us()`；`trigger_vector.h` 和 `sync_trigger.h` 对历史 marker event/field 增加 deprecated RJ45 compat 注释。
- 完成：同步更新 `SYNC_IO_REFACTOR_PLAN.md`、`SYNC_IO_RESOURCE_PLAN.md`、`SCPI_COMMANDS.md`、`SYNC_IO_ARCH_REVIEW_TODO.md`、HAOFV 文档、BiSS 硬件约束文档和 Trigger 待办，明确 AUX3 不再是 marker 目标。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707170752` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`picotool reboot -f -u` 后 `picotool load -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录并启动应用成功。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_rj45_hw_definition` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_rj45_hw_definition` 通过，确认 BiSS TAP 与 RJ45 trigger 语义入口未回退。
- 风险：SCPI/UI/TriggerVector 仍保留 `MARK:*` / `marker_width_us` 历史命名；短期作为 ABI 兼容保留，后续可新增正式 `RJ45:*` 命令再逐步 deprecated。
- 后续：如继续清理命名，应先增加 `RJ45:*` SCPI/UI 入口和状态字段，再保留 `MARK:*` 作为兼容别名，不改动 `GPIO23/RJ45_TRIG_OUT` 硬件定义。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/inc/sync_trigger.h`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/communication/BISSC_TAP_BRIDGE_DESIGN.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`。

### SYNC-PROGRESS-20260708-009 - ENC_COUNT 3-pin 软件定义收口

- TODO task ID：`SYNC-MODE-001`、`SYNC-PROFILE-001`。

- 目标：按硬件定义优先原则固定 `GPIO19/RJ45_TRIG_IN`，将 ENC_COUNT 软件定义收口为 A/B/Z=`GPIO16/GPIO17/GPIO18`，避免 ENC 再占用 IN3。
- 完成：`SYNC_IO_HW_ENC_Z_PIN` 改为 `BOARD_SYNC_INPUT_BASE_PIN + 2`；`sync_io_hw_enc_pins_valid()`、TriggerVector 默认值和 `TRIG:ENC:APIN` 事件载荷均使用 A=16、B=17、Z=18。
- 完成：`enc_count.pio` 从 4-pin 采样改为 3-pin contiguous 采样，Z 从 bit2 提取；PIO init/disarm 只初始化和释放 GPIO16..18，不再触碰 GPIO19。
- 完成：`sync_io_enc_count_mode_validate()` 只接受 `in_pin_base=16` 且 Z=`base+2`；`TRIG:ENC:APIN 26` 继续作为关闭能力返回执行错误。
- 完成：同步更新 HAOFV、SYNC_IO、SCPI、BiSS 和 Trigger 文档，明确 `GPIO19` 是 `RJ45_TRIG_IN`，`GATE_IN` 只是模式层解释，ENC Z 不再使用 IN3。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707172833` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`git diff --check boards/rp2350_trig/inc/board_config.h components/sync_io/inc/sync_io_hw_profile.h components/sync_io/src/enc_count.pio components/sync_io/src/sync_io_mode_enc_count.c components/sync_trigger/inc/trigger_vector.h docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md docs/arch/HAOFV_ARCHITECTURE.md docs/interface/SCPI_COMMANDS.md docs/sync/SYNC_IO_ARCHITECTURE.md docs/sync/SYNC_IO_TODO.md docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md docs/communication/BISSC_TAP_BRIDGE_DESIGN.md docs/communication/BISSC_IMPLEMENTATION_TODO.md docs/trigger/TRIGGER_SYNC_TODO.md docs/trigger/TRIGGER_ENC_COUNT_DESIGN.md docs/trigger/TRIGGER_PULSE_COUNT_ANALYSIS.md` 通过，仅有既有 CRLF warning。
- 验证：`picotool reboot -f -u` 后 `picotool load -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录并启动应用成功；板端 `SYST:FW:BUILD?` 返回 `"20260707172833"`。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_enc_3pin_pinout_final` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_enc_3pin_pinout_final` 通过，确认 BiSS TAP 未被 ENC pinout 调整回退。
- 验证：板端 `TRIG:ENC:APIN?` 返回 `16,17,18`；执行 `TRIG:ENC:APIN 26` 后 `SYST:ERR?` 返回 `-200,"Execution error"`，再次查询仍为 `16,17,18`。
- 风险：`docs/archive/TASK_PROGRESS.md` 中仍保留迁移前历史记录的旧 ENC 16/17/19 描述；按文档规则该文件作为全局历史保留，不作为当前硬件约束入口。
- 后续：如继续推进 P2 自检，应在板端闭环脚本中增加 ENC A/B/Z loopback 或外部回放验证，覆盖真实 A/B/Z 脉冲输入，而不仅是 SCPI 配置与资源 owner 断言。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/enc_count.pio`，`components/sync_io/src/sync_io_mode_enc_count.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

### SYNC-PROGRESS-20260708-010 - SYNC_CLK_OUT AUX2 运行路径迁移

- TODO task ID：`SYNC-PROFILE-001`。

- 目标：完成 P2-04 中 `SYNC_CLK_OUT` 从旧 GPIO22/`pio1/sm1` 到 AUX2/GPIO28/`pio2/sm2` 的运行路径迁移，保持硬件定义优先。
- 完成：`BOARD_SYNC_SYNC_CLK_OUT_PIN` 解析到 `BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`；新增 `BOARD_SYNC_MODE_OUT2_PIN` 表达 GPIO22 仍是主口 OUT2/模式本地输出。
- 完成：`sync_io_start_clock()` 改用 `BOARD_SYNC_PIO_AUX`、`BOARD_SYNC_AUX2_SM`、`BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`，并在启动期间持有 `PIO2 + AUX` 资源；停止时释放资源并恢复 AUX2 输入安全态。
- 完成：TriggerFB 在 `OUTP:CLOC:*` 对应事件中检查 `sync_io_start_clock()` 结果，失败时同步真实 clock 状态并设置 `TRIG_ERROR_RESOURCE_CONFLICT` 或 `TRIG_ERROR_IO_ARM_FAILED`。
- 完成：`sync_io_hw_profile.h` 增加主口、RJ45_TRIG_IN/OUT、ARM_IN、EXT_CLK_IN、SYNC_CLK_OUT、AUX3 和 deprecated marker alias 的编译期断言。
- 验证：`cmake --build build-codex-rj45-interface` 通过，生成 factory/update 产物。
- 风险：`ARM_IN`、`EXT_CLK_IN` 仍是语义占位，旧低层宏只做 pull-down/诊断采样；后续需要迁移到 AUX0/AUX1 并接入 TriggerFB 资格/外部时钟逻辑。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

### SYNC-PROGRESS-20260815-003 - ModelTurntableAO PIO 预约输出首版

- TODO task ID：`SYNC-OUT-001`。

- 目标：完成当前 P0 的第一段闭环，让 `ModelTurntableAO` 不再依赖 `time_us_64()` 主循环软件翻 GPIO 输出脉冲，而是生成 bounded pulse plan，由 `sync_io` realtime primitive 使用 PIO/DMA 到点输出边沿。
- 完成：新增 `sync_io_model_sched.c`，提供 `sync_io_model_pulse_schedule_arm/disarm/is_running/get_runtime`；首版使用 `pio1/sm1 + DMA2`，1 MHz tick，支持 `delay_us`、`high_us`、上升/下降有效边沿、最多 256 个脉冲和 PIO/DMA/FIFO runtime snapshot。
- 完成：`sync_io.pio` 增加 active-high / active-low 两个预约脉冲程序；`board_config.h` 明确 `BOARD_SYNC_MODEL_SCHED_SM=1`，与产品主触发 `pio1/sm0` 隔离。
- 完成：`ModelTurntableAO` 改为在 start 时根据扫描起止、步长、速度/加速度和脉宽生成计划并提交给 `sync_io`；service 只读取 runtime snapshot 更新 emitted/phase/current_position，不再直接写 debug GPIO。
- 验证：`cmake --build build-rtos-multicore-smoke` 通过，生成 `RP2350_TRIG_FACTORY.uf2` 和 `RP2350_TRIG_UPDATE.pkg`。
- 风险：本轮只完成编译闭环，尚未烧录和示波/loopback 验证真实边沿；late/drop/overflow 计数、RESET 统一 release、Host C 计划生成断言仍在 P0/P3 待办。
- 后续：优先补单板或两板 loopback HIL，捕获模型转台输出脉冲数量、宽度、完成态和 abort；随后再推进 RefMem 真实最小 transport，不继续扩大静态表模型。
- 涉及文件：`CMakeLists.txt`，`boards/rp2350_trig/inc/board_config.h`，`components/model_turntable/src/model_turntable.c`，`components/sync_io/inc/sync_io.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io.pio`，`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io_model_sched.c`，`docs/sync/SYNC_IO_ARCHITECTURE.md`，`docs/sync/SYNC_IO_TODO.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

## 失败与回退

当前文档重构不改变固件运行态，无代码回退动作。后续 persona 迁移如果出现 PIO instruction
space 不足、SM/DMA 冲突、GPIO function 被 analyzer 接管、TDMA cycle/CRC 回归或 capture
不可定位丢失，必须停止新 persona、保存 snapshot 和原始波形，并恢复最近通过统一硬件验收
的资源配置。不得用旧 PIO1 wave handoff 掩盖 PIO0 persona manager 的缺口。

## 下一 Gate

先完成 `SYNC-DOC-001` 的文档门禁和提交，再进入 `SYNC-RES-001`。`SYNC-RES-001` 只建立
descriptor、容量计算和冲突负测，不同时搬迁输出 PIO；通过快速 TDMA 短帧闭环后，才进入
`SYNC-RES-002` 生命周期实现。
