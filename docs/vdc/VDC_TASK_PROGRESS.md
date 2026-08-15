# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-16

本文档记录 Virtual Distributed Clock / VDC Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `VDC_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 记录规则

每条任务记录使用以下格式：

```text
### VDC-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

VDC Domain 当前目标是把 VDC/DPLL 从同步域中的基础算法，升级为 HAOFV 内部基础主域：

```text
VdcSyncAO
+ SyncDpllFB
+ HoldoverFB / RelockFB
+ VdcVector
+ VdcClockModel
+ VdcQualityTable
+ TimestampDictionary
```

首阶段先完成文档主域和架构边界，并把现有 RefMem/TDMA 诊断字段收敛到不会冒充 100 ns DPLL evidence 的代码形态。

## 任务记录

### VDC-TASK-20260816-006 - RefMem TDMA data frame to VDC envelope bridge

- 状态：完成 host 验证；仍为诊断 timestamp，硬件 latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 将 COM5/COM6 已跑通的 RefMem TDMA data frame 纳入 VDC frame envelope/evidence 体系。
  - 保持 HAOFV 边界：RefMem 只映射 payload/evidence，VDC gate 只校验时间契约，offset/rate 仍只能由 `VdcSyncAO / SyncDpllFB` 写。
- 完成内容：
  - 新增 `refmem_vdc_bridge` 基础件，把 `REFMEM_SYNC_FRAME_DELTA` 映射为 `VDC_DOMAIN_PAYLOAD_REFMEM_DELTA`，把 `ACK_NACK/FENCE/QUALITY` 映射为 `VDC_DOMAIN_PAYLOAD_ACK_NACK_FENCE_QUALITY`。
  - 新增独立适配头 `distributed_refmem_vdc_bridge.h`，避免 `distributed_refmem.h` 直接依赖 VDC 主域头文件。
  - 新增 `distributed_refmem_build_realtime_tdma_vdc_envelope()`，从最近 TDMA RX result frame 构造 `vdc_tdma_frame_envelope_t`。
  - 新增维护查询 `SYSTem:REFMEM:SYNC:TDMA:VDC? [local_slot],[reference_slot]`，输出 bridge result、VDC gate result、frame/window/payload、CRC 和 timestamp 摘要。
  - 诊断 timestamp 仍保持 `SOFTWARE_US`、`timestamp_resolution_ns=1000`、`DIAGNOSTIC_ONLY`，可通过 VDC frame envelope 质量检查，但要求 DPLL eligibility 时会被拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_vdc_bridge_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\checks\check_scpi_usb_namespace.py --root .` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815181647`，package CRC `0x48C97051`。
- 还需完成：
  - 将 PIO/DMA/IRQ/core1 硬件 timestamp latch 接入 bridge 输入，使 `timestamp_resolution_ns <= 100` 的 `VDC_OBSERVATION_WINDOW` 样本能够进入正式 DPLL gate。
  - 增加 HIL 脚本查询 `SYSTem:REFMEM:SYNC:TDMA:VDC?`，记录 COM5/COM6 的 frame CRC、payload CRC、late/delay 和 gate result。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_vdc_bridge.h`
  - `components/distributed_refmem/src/refmem_vdc_bridge.c`
  - `components/distributed_refmem/inc/distributed_refmem_vdc_bridge.h`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_vdc_bridge.c`
  - `tools/tests/run_refmem_vdc_bridge_tests.ps1`
- 下一步：
  - 跑 docs/diff/build 验证，通过后提交推送；随后进入硬件 timestamp latch 或 HIL 查询脚本固化。

### VDC-TASK-20260816-005 - VDC DCO control snapshot contract

- 状态：完成 host 验证；core1/PIO 实际消费待实现
- 日期：2026-08-16
- 任务目标：
  - 将 DPLL 输出给 core1/PIO 的 DCO 控制快照从文档契约落到 C 结构。
  - 明确 `SyncDpllFB` 侧的 clock model / DCO snapshot 是唯一受控输出，core1 后续只读稳定 snapshot。
- 完成内容：
  - 新增 `vdc_dco_control_t`，覆盖 `base_local_tick64`、`base_vdc_time64_ns`、`nominal_period_ns`、`period_adjust_ppb`、`phase_offset_ns`、`slew_limit_ppb`、`dco_update_seq`、`source_model_seq`、`epoch_id/run_id`、lock state 和 profile CRC。
  - `vdc_domain_context_t` / `vdc_domain_snapshot_t` 增加 `dco` 字段。
  - 新增 `vdc_domain_default_dco_control()`、`vdc_domain_dco_control_validate()` 和 `vdc_domain_publish_dco_control()`。
  - `vdc_domain_publish_clock_model()` 会从新的 `VdcClockModel` 派生 DCO snapshot，并由 VDC owner 单调递增 `dco_update_seq`。
  - DCO validate 拒绝无效 snapshot、CRC 不匹配、非法 lock state 和超过 servo sanity limit 的 slew。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815180531`，package CRC `0x3512BD4E`。
- 还需完成：
  - core1 realtime 读取 DCO snapshot 时增加 seqlock、双缓冲或等价 guard。
  - 将 DCO snapshot 接到 `FIRE_LOAD` 预测路径，验证 late/半更新 snapshot 不会输出边沿。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 跑全量 host/build 验证；通过后提交推送，再继续硬件 timestamp latch 或 RefMem TDMA frame envelope 接入。

### VDC-TASK-20260816-004 - TDMA frame envelope and window class contract

- 状态：完成 host 验证；硬件 PIO timestamp latch 待接入
- 日期：2026-08-16
- 任务目标：
  - 将“每一帧首先是 TDMA/VDC frame envelope，RefMem 只是 payload class”落成 C 契约。
  - 将 `VDC_OBSERVATION_WINDOW`、`REFMEM_DATA_WINDOW` 和 `IDLE_BEACON` 纳入同一 `VdcTdmaScheduleProfile`。
  - 防止 RefMem data frame 或诊断 timestamp 被当成 DPLL 样本进入 lock gate。
- 完成内容：
  - `VdcTdmaScheduleProfile` 增加 RefMem data window 和 idle beacon window 的 offset/width 字段，schedule CRC 覆盖所有窗口定义。
  - 新增 `vdc_domain_tdma_window_class_t` 和 `vdc_tdma_frame_envelope_t`。
  - 新增 `vdc_domain_validate_tdma_frame_envelope()`，校验 frame version、schedule epoch/CRC、source/reference slot、window class、payload class、frame/payload CRC、timestamp source/resolution 和窗口边界。
  - `VDC_OBSERVATION_WINDOW` 只允许 `SYNC_SAMPLE/IDLE_BEACON`，并复用 timestamp evidence gate；要求 DPLL eligibility 时必须是硬件 tick、分辨率不大于 `100 ns`。
  - `REFMEM_DATA_WINDOW` 只允许 `REFMEM_DELTA/ACK_NACK_FENCE_QUALITY`，可以携带诊断 timestamp 和 quality evidence，但在要求 DPLL 样本时必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815180148`，package CRC `0x4AF89705`。
- 还需完成：
  - 将真实 PIO/DMA/IRQ/core1 timestamp latch 接到 frame envelope 的 `timestamp` 字段。
  - 将 frame envelope 接入现有 RefMem PIO SPI adapter 的 TX/RX 路径，使 COM5/COM6 板端同步报告不再依赖 host 侧耗时。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 增加 DCO snapshot / core1 消费契约，或者先把 frame envelope 映射到 RefMem TDMA adapter 的板端 evidence 输出。

### VDC-TASK-20260816-003 - VDC domain core contract landing

- 状态：完成 host/build 验证；硬件 timestamp latch 和真实 servo 待实现
- 日期：2026-08-16
- 任务目标：
  - 把 VDC 从 `vdc_dpll_manager` 的 ready/service stub 推进为独立 HAOFV 内部基础组件。
  - 冻结并落地 `VdcClockModel`、`VdcTdmaScheduleProfile`、`VdcTDMATimestampEvidence`、`VdcServoProfile`、`VdcDpllState` 和 `VdcGateResult` 的首版 C 契约。
  - 实现 `local_tick -> vdc_time64_ns` 映射和 TDMA observation window 输入门禁，明确诊断时间戳不得进入 DPLL。
- 完成内容：
  - 新增 `components/vdc_domain/inc/vdc_domain.h` / `src/vdc_domain.c` 和组件 `CMakeLists.txt`。
  - 新增 schedule CRC、schedule validate、默认 schedule/servo/clock model、clock mapping、timestamp evidence gate 和最小 DPLL 状态推进。
  - 门禁要求 active schedule、schedule CRC、epoch、reference slot、source slot、payload class、hardware timestamp source、`DPLL_ELIGIBLE` flag、`timestamp_resolution_ns <= 100` 和 observation window bound 全部通过。
  - 旧 `components/vdc_dpll_manager/` 改为兼容 wrapper，应用任务和现有 `SYSTem:SYNC:VDC:*?` 查询继续走旧接口，但状态来源开始接入 `vdc_domain`。
  - 新增 `tests/unit/test_vdc_domain.c` 和 `tools/tests/run_vdc_domain_tests.ps1`，并加入 `run_host_unit_tests.ps1`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，16/16 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815175522`，package CRC `0xB090BCE7`。
- 还需完成：
  - 将 PIO/DMA/IRQ/core1 的真实 timestamp latch 输出接入 `vdc_domain_submit_tdma_evidence()`。
  - 实现 `SyncDpllFB` 的真实 offset/rate servo，而不是当前 sample-count 分阶段状态推进。
  - 将 VDC snapshot 映射到 RefMem VDC/DPLL 区域，并完善 SCPI 字段。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tests/unit/test_vdc_domain.c`
  - `tools/tests/run_vdc_domain_tests.ps1`
- 下一步：
  - 增加板端帧级 timestamp evidence 输入路径，先让 COM5/COM6 在真实 PIO TDMA 环路中产出被 VDC gate 明确拒绝或接受的样本证据。

### VDC-TASK-20260816-002 - TDMA diagnostic timestamp and 1e3ns deadline contract

- 状态：完成 host/build 验证；硬件 latch 和 DPLL gate 待实现
- 日期：2026-08-16
- 任务目标：
  - 将 RefMem realtime TDMA 的 deadline 接口收敛为 `deadline_1e3ns`，与 VDC 以 ns 为基础单位的命名一致。
  - 给当前 TDMA snapshot 增加板端时间戳诊断字段，但明确来源仍是 `time_us_64()*1000`，分辨率为 `1000 ns`，不得作为 100 ns DPLL lock evidence。
  - 保持 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 既有字段顺序稳定，新增 timestamp 字段只追加。
- 完成内容：
  - `refmem_realtime_tdma` 新增 `timestamp_source`、`timestamp_resolution_ns`、`timestamp_flags`、submit/core1 arm/start/done 时间和 `core1_elapsed_ns` snapshot。
  - `distributed_refmem`、RefMem sync frame、RefMem command timeout、quality snapshot、SCPI handler 和 HIL 脚本统一使用 `deadline_1e3ns` / `timeout_1e3ns` / `p99_1e3ns` / `p999_1e3ns` 语义。
  - PIO SPI physical adapter 的公开 timeout 参数收敛为 `timeout_1e6ns`；内部仍使用 Pico SDK 的微秒 API 执行等待，但接口语义不再暴露 `timeout_ms`。
  - VDC 文档补充每帧都是 TDMA/VDC envelope，DPLL 维护是总线时序骨架，RefMem 作为 payload class 搭载。
- 验证结果：
  - `python -m py_compile tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py tools\refmem_quality_gate_hil_validate\refmem_quality_gate_hil_validate.py tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，15/15 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815174141`，package CRC `0x58997C54`。
- 还需完成：
  - 实现 PIO/DMA/IRQ/core1 硬件 timestamp latch，使正式 DPLL 样本满足 `timestamp_resolution_ns <= 100`。
  - 增加 COM5/COM6 板端脚本，采集每帧 payload class、schedule CRC、frame/sample CRC、late/jitter 和 phase error。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 冻结 `VdcTDMATimestampEvidence` C 结构和正式 `VDC_OBSERVATION_WINDOW` sample gate。

### VDC-TASK-20260816-001 - Two-board TDMA baseline to DPLL input contract

- 状态：完成文档收敛；代码和板端 DPLL 采样待实现
- 日期：2026-08-16
- 任务目标：
  - 明确 COM5/COM6 两板真实 TDMA/PIO 环路已经形成后续 DPLL 的硬件基础。
  - 防止把 RefMem frame 同步成功、host 侧耗时或微秒软件时间戳误判为 100 ns 级 DPLL evidence。
  - 将 RefMem data TDMA window 和 VDC observation window 收敛为同一总线 TDMA cycle 下的不同 window class，并明确帧级 TDMA/VDC envelope。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 Two-board TDMA hardware baseline，记录无 CS 3-wire PIO SPI、25 MHz、core1 realtime TDMA service 和 RefMem `HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY` 的关系。
  - 定义 TDMA frame envelope：每一帧先表达 schedule、slot、frame_seq、timestamp evidence、CRC 和 payload class，RefMem 只是 payload。
  - 定义两类 TDMA window class：`VDC_OBSERVATION_WINDOW` 用于高优先级 DPLL timestamp observation，`REFMEM_DATA_WINDOW` 在同一 VDC/TDMA 骨架上同步共同事实。
  - 明确总线在同步数据过程中也维护 DPLL；总线在循环维护 DPLL 时可以顺带同步 RefMem；无业务数据时仍需要 `IDLE_BEACON` 或等价同步帧维持 freshness。
  - 明确 DPLL 只能消费板端硬实时 timestamp sample；`time_us_64()*1000` 只能作为诊断时间戳，必须报告 `timestamp_resolution_ns=1000`，不得作为 100 ns evidence。
  - `VDC_DOMAIN_TODO.md` 增加 `VdcTDMATimestampEvidence`、两板 observation window bring-up、timestamp resolution gate 和 COM5/COM6 板端 evidence 验证项。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
- 还需完成：
  - 实现 PIO/DMA/IRQ/core1 timestamp latch，使 `timestamp_resolution_ns <= 100` 的样本进入 DPLL gate。
  - 增加 COM5/COM6 板端脚本，记录每帧 expected/observed/apply timestamp、late/jitter、sample CRC、payload class 和 phase error。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 清理当前 TDMA timestamp 代码草稿，确保过渡诊断字段不冒充 100 ns DPLL evidence；随后进入 `VDC_OBSERVATION_WINDOW` 的 C 结构和 SCPI snapshot 落地。

### VDC-TASK-20260814-005 - TDMA + DPLL 融合架构补充

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 TDMA 与 DPLL 的融合架构输入纳入 VDC canonical，而不是把二者作为可替代方案。
  - 明确 TDMA、DPLL 和低频驯服环的分层职责、writer 边界和数据契约。
  - 将性能描述收敛为待验证目标，避免在未实测前写成产品保证。
- 完成内容：
  - `README.md` 增加当前主线摘要：TDMA 硬实时环、DPLL 锁相环、低频驯服环和 core0/core1/PIO 边界。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 `TDMA + DPLL 融合控制模型`，定义三层控制环、TDMA observation window、DPLL servo/DCO contract、low frequency discipline 和 fused state machine。
  - `VDC_DOMAIN_ARCHITECTURE.md` 的内部数据模型新增 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel`，核心字段增加 TDMA schedule CRC、DCO update seq、period adjust、slew limit、aging/temperature compensation 和 holdover drift bound。
  - 状态机从单一 `LOCKING` 细分为 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED`，便于后续分别验证初始同步、频率拉入、相位收敛和正式锁定。
  - `VDC_DOMAIN_TODO.md` 增加 TDMA schedule profile、DCO snapshot、observation window gate、低频驯服任务和对应验证项。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
- 还需完成：
  - 冻结 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel` 的 C 结构、字段单位和 snapshot guard。
  - 代码中实现 TDMA observation window 输入门禁和 DCO snapshot 提交。
  - 增加收敛验证脚本，记录 lock_time、RMS/peak offset、outlier ratio、DCO slew 和 HOLDOVER drift bound。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 按 P2 先冻结 VDC 数据契约，再进入 P3 的 DPLL/Clock Model 代码落地。

### VDC-TASK-20260813-004 - PIO/VDC 首版硬实时装配链补充

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将首版 `PIO_SM0: SYNC_RX_CAPTURE`、`PIO_SM1: SYNC_TX_FIRE`、DMA、core1 realtime、`task_vdc_sync` 和 `task_loop_engine` 的参考数据通路写入 VDC 主域。
  - 明确硬实时采样/输出链路和软件 DPLL/VDC owner 的边界。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加“首版 PIO/VDC 参考装配链”，定义捕获链、DPLL 更新链和 FIRE_LOAD 输出链。
  - `RTOS_HAOFV_ARCHITECTURE.md` 增加 PIO/VDC 首版数据通路表和数据流。
  - `VDC_DOMAIN_TODO.md` 增加 PIO_SM0 FIFO/DMA/timestamp、PIO_SM1 FIRE_LOAD 小载荷、core1 写入边界和输出链路冻结待办。
  - `VDC_DOMAIN_TODO.md` 增加 PIO capture 到 VdcSlot、LoopEngine 到 PIO_SM1 的验证项。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 后续根据实际 PIO 资源、布线和 board profile 调整具体 PIO instance、GPIO、DMA channel 和 ring buffer 大小。
  - 后续在代码中定义 `FIRE_LOAD` 小载荷和 capture FIFO word 格式。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- 下一步：
  - 进入 VDC P2/P4，把 timestamp sample、DPLL input ring 和 `FIRE_LOAD` payload 字段契约冻结。

### VDC-TASK-20260813-003 - 虚拟 DC 时钟参考框架补足

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 基于 LinuxPTP / Chrony / SOEM EtherCAT DC 的时间同步机制，补足 VDC 主域框架。
  - 将参考项目落到 reference clock、servo profile、error budget、DC sync pipeline 和 HOLDOVER model。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 VDC 框架补足章节。
  - 定义 `VdcReferenceClockTable`，首版可固定 A0，后续支持 candidate、priority、source 和 failover reason。
  - 定义 `VdcServoProfile`，覆盖 servo type、kp/ki、update period、step threshold、sanity frequency limit、lock threshold、outlier threshold 和 reset policy。
  - 定义 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、delay stddev、dispersion 和 root distance 等价误差上界。
  - 定义 `VdcDcSyncPipeline`，覆盖 reference select、active calibration delay、timestamp dictionary/profile CRC、initial sync、drift compensation、LOCKED publish 和 T2/READY validation。
  - `VDC_DOMAIN_TODO.md` 补充 reference clock、servo profile、error budget、DC sync pipeline 和 holdover drift bound 待办。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 进入 VDC P2，冻结 `VdcClockModel`、`VdcServoProfile`、`VdcErrorBudget` 和 `VdcDcSyncPipeline` 字段契约。

### VDC-TASK-20260813-002 - VDC 外部时间同步参考拆分

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将刚刚列出的外部参考项目按虚拟反射内存和虚拟 DC 时钟拆分。
  - 把 LinuxPTP / Chrony、SOEM / EtherCAT DC 和 VDC owner/FB 边界参考归入 VDC Domain。
- 完成内容：
  - `VDC_DOMAIN_TODO.md` 增加“参考项目收敛原则”，明确 VDC 只吸收 offset/rate、jitter、servo reset、HOLDOVER、reference clock、传播 delay、initial sync、drift compensation 和 timestamp 机制。
  - `VDC_DOMAIN_TODO.md` 增加 P1.5 外部时间同步参考机制工程化收敛章节。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加外部参考机制矩阵，区分 LinuxPTP/Chrony、SOEM/EtherCAT DC 和 IEC 61499 的 VDC 落地方式与不采用内容。
  - VDC P8 增加 PTP/Chrony-style 和 EtherCAT DC-style 验证项。
- 验证结果：
  - 本任务为文档拆分，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加 VDC/DPLL 质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 EtherCAT DC-style initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 按 VDC P2/P3 冻结 VDC 数据契约和 DPLL/clock model。

### VDC-TASK-20260813-001 - VDC 三份标准文档建立

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 `docs/vdc/` 中建立 VDC 内部主域的三份标准文件和目录 README。
  - 将“虚拟 DC 时钟升格为 HAOFV 内部基础主域”的边界写入文档。
- 完成内容：
  - 新增 `VDC_DOMAIN_ARCHITECTURE.md`，定义 VDC Domain 的定位、职责边界、HAOFV 层级、内部数据模型、共同时间映射、状态机、跨域契约、SCPI 边界、目标代码形态和验证门禁。
  - 新增 `VDC_DOMAIN_TODO.md`，把文档同步、HAOFV 主域升级、数据契约、DPLL、CAL/SYNC/MEAS/TRIG 边界、RefMem 映射、代码组件化、SCPI/System Pack 和验证拆成 P0-P8 待办。
  - 新增 `VDC_TASK_PROGRESS.md`，作为 VDC 主域独立任务进度入口。
  - 新增 `README.md`，作为 VDC 目录入口。
  - 更新 `docs/README.md`、`docs/arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 VDC 从 `sync/` 中拆出为内部基础主域。
  - 更新 `HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_TODO.md`、`HAOFV_MAINTENANCE_TODO.md`、`sync/README.md` 和 RefMem 入口，明确 VDC 与 RefMem 并列：VDC 管共同时间，RefMem 管共同事实。
- 验证结果：
  - 本任务为文档生成，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 运行 `python tools/docs_check/docs_check.py`。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
- 下一步：
  - 按 `VDC_DOMAIN_TODO.md` 的 P0/P1 更新索引和架构入口。
