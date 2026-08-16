# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-17

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

### VDC-TASK-20260817-005 - VDC accepted-count capture DMA produced closure

- 状态：完成；accepted-count 断点已定位、修复、构建、OTA，并用 COM5/COM6 GPIO overlay accepted-only HIL 两向复验。
- 日期：2026-08-17
- 任务目标：
  - 将 EtherCAT DC-style 对照项纳入 `VDC_DOMAIN_TODO.md` 后，优先解决两板 GPIO overlay HIL 中 `accepted_sample_count=0` 的问题。
  - 保持 HAOFV 边界：修复 SYNC_IO capture/latch 基础件，不绕过 VDC gate 或伪造 DPLL accepted sample。
- 发现：
  - `vdc_gpio_lock_validate.py` 失败时 model pulse 已完成 4096 个脉冲，但 observer raw/submitted/accepted 计数全为 0。
  - 静态 probe 证明 COM5 GPIO6 可被 COM6 GPIO6 读到，物理 debug overlay 可用。
  - `REALtime:IO:SAMPle:DBG?` 显示 capture running、PIO enabled、DMA busy，但 `produced_words=0` 且 `dma_transfer_count=0xFFFFFFFF`。
  - 根因是 `sync_io_capture_dma_produced_words()` 在 `transfer_produced == produced_seq == 0` 时直接采用 transfer counter，导致无限 DMA 模式下永远不走 ring write-index delta。
- 完成内容：
  - 将 `transfer_produced >= capture_dma_produced_seq` 改为严格 `>`；只有 transfer counter 真正前进时才采用它，否则继续用 write index delta 推导 produced words。
  - 固化 `tools/scpi_query/model_gpio_probe.scpi`、`model_gpio_drive_g6.scpi` 和 `sample_capture_debug_probe.scpi`，用于后续低频/SCPI 断点复查。
  - `vdc_gpio_lock_validate.py` 增加 `--accepted-only` 分层验收：只验证本轮 observer accepted 增长、VDC quality accepted 非零、gate/source/resolution/flags 为硬件 DPLL eligible；完整模式仍要求 `LOCKED + HEALTHY + FINE_100NS`。
- 验证结果：
  - Host 单元：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过 18/18。
  - 文档检查：`python tools\docs_check\docs_check.py` 通过；仍有既有 risk-review 文件名 warning。
  - 固件构建：`cmake --build build-rtos-multicore-smoke -j 4` 通过，最终 build id `20260816175137`，package CRC `0xFB86122E`。
  - OTA：`python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --expected-build 20260816175137 --max-workers 2 --verbose` 通过；COM5/COM6 均升级并提交到 `20260816175137`，两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - capture debug：COM6 `REALtime:IO:SAMPle:DBG?` 中 `dma_transfer_count=4294967295` 时 `produced_words` 已增长到 `18887974`，证明 infinite DMA ring produced accounting 已恢复。
  - 两板 HIL：`python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --name-x COM5 --name-y COM6 --expected-build 20260816175137 --poll-timeout 90 --output-index 2 --observed-mask 4 --pulse-count 4096 --pulse-high-ns 1000 --accepted-only --reverse` 通过；COM5->COM6 `accepted=0->1 observer_accepted=0->1 state=PHASE_LOCK locked=0 health=DEGRADED tier=NONE source=HARDWARE_TICK resolution_ns=100 flags=DPLL_ELIGIBLE gate=PASS`，COM6->COM5 `accepted=0->4 observer_accepted=0->4 state=LOCKED locked=1 health=LOCK_CANDIDATE tier=NONE source=HARDWARE_TICK resolution_ns=100 flags=DPLL_ELIGIBLE gate=PASS`。
- 还需完成：
  - accepted path 已闭环；后续不再把 `accepted_sample_count=0` 作为当前阻塞点。
  - `HEALTHY/FINE_100NS` 仍未完成，归入 P0.5：补齐 DC-style reference sync frame、delay-measure、active `PATH_DELAY` 表和 DCO 反驱 TDMA 后继续优化 DPLL 锁相。
- 关联文件：
  - `components/sync_io/src/sync_io.c`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
  - `tools/scpi_query/model_gpio_probe.scpi`
  - `tools/scpi_query/model_gpio_drive_g6.scpi`
  - `tools/scpi_query/sample_capture_debug_probe.scpi`
- 下一步：
  - 进入 P0.5 DPLL/DC 锁相优化；本轮不再扩大 SCPI 或脚本旁路。

### VDC-TASK-20260817-004 - DPLL lock quality residual and slew-limit closure

- 状态：完成代码、host 单元验证、构建、文档检查、COM5/COM6 OTA、只读 SCPI/HIL 复查和 EtherCAT DC-style 对照评审；板端 HIL 仍卡在 timestamp eligibility，需在后续硬件 timestamp latch/observer 链路继续收敛。
- 日期：2026-08-17
- 任务目标：
  - 纠偏 `VDC_DOMAIN_RISK_REVIEW.md` 中 `锁质量用残差非入相` 和 `slew_limit 硬编码` 两项风险。
  - 保持 HAOFV 边界：SCPI/脚本只配置和读取镜像，`SyncDpllFB` 才能写 clock model、DCO snapshot、lock 和 quality。
- 完成内容：
  - `vdc_domain_submit_tdma_evidence()` 在每帧 accepted path 中先计算更新前入相残差；lock state、quality tier、offset/error budget 和 outlier 口径均使用该入相残差。
  - `vdc_domain_update_clock_from_evidence()` 使用更新前入相残差驱动 phase correction 和 KI rate pull，避免 raw phase 在 offset 已抵消后继续重复拉频。
  - `clock.slew_limit_ppb` 在 init、accepted evidence 更新和 `publish_clock_model()` 中统一跟随 active `servo.sanity_freq_limit_ppb`；默认 sanity limit 提为命名常量。
  - 单测更新 `sync_io` compact observation 的入相 phase 口径，并新增 90 us first-step 回归：大阶跃允许快速拉入，但不能同帧或未连续稳定时发布 `FINE_100NS/HEALTHY`。
  - 对照 EtherCAT DC 拆解当前 VDC：已具备 `LOCAL_TIME + OFFSET + DRIFT_CORR` 的 clock model 雏形和 TDMA observation payload 骨架；仍缺 reference sync frame 的时间语义、delay-measure 沿途 timestamp、active `PATH_DELAY` 表、`T_local_rx + OFFSET - (T_reference + PATH_DELAY)` 显式公式，以及 DCO 反驱 core1 TDMA frame/slot。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 RP2350 仿 DC 五层拆解；`VDC_DOMAIN_TODO.md` 增加 EtherCAT DC-style 对照缺口和低频 log 记录项。
- 验证结果：
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --name-x COM5 --name-y COM6 --expected-build 20260816165320 --poll-timeout 90 --pulse-count 4096 --pulse-high-ns 1000 --reverse` 基线失败：`accepted count did not grow: 0 -> 0`；readiness/observer 显示当前仍未形成 DPLL accepted hardware evidence。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816171400`，package CRC `0x4FEAE6F7`。
  - `python tools\docs_check\docs_check.py` 通过；保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均升级并 commit 到 build `20260816171400`。
  - COM5/COM6 只读 SCPI 复查通过：`SYSTem:ERRor?` 均为 `0,"No error"`，`SYSTem:SYNC:VDC:DCO?` valid，`clock/dco.slew_limit_ppb=50000` 与 active servo sanity limit 一致。
  - 新固件 HIL 复跑仍失败于 COM5->COM6：`accepted count did not grow: 0 -> 0`。HIL 后 COM5 `SYSTem:SYNC:VDC:LOCK:READiness?` 显示 reject code `9`（`TIMESTAMP_NOT_ELIGIBLE`），`SYSTem:SYNC:VDC:TDMA:STATus?` 显示 VDC sync sample 活动，说明后续断点在 TDMA timestamp spine / hardware latch eligibility，而不是本轮 DPLL quality 口径。
- 还需完成：
  - 观察低频 log / VDC readiness / observer / latch 镜像，定位 accepted count 不增长的硬件 timestamp eligibility 断点。
  - 补齐维护域低频 log 对 VDC/TDMA/observer 关键镜像的周期落盘，作为后续锁相优化的异常调查基础。
- 关联文件：
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_DOMAIN_RISK_REVIEW.md`
- 下一步：
  - 优先定位 `TIMESTAMP_NOT_ELIGIBLE` 断点，使 TDMA observation window 的硬件 latch 样本进入 accepted path；随后实现 reference sync frame、delay-measure frame 和 active `PATH_DELAY` 表。

### VDC-TASK-20260817-003 - sync_io P2 DMA and counter safety closure

- 状态：完成代码、host 单元验证、构建、文档检查、COM5/COM6 OTA 和只读 SCPI 复查。
- 日期：2026-08-17
- 任务目标：
  - 纠偏 `VDC_DOMAIN_RISK_REVIEW.md` 中剩余的三项明确 P2 bug：capture DMA 整圈丢字、ENC_COUNT 查询踩 ISR、模型 pulse scheduler 完成计数饱和。
  - 保持 HAOFV 实时边界：查询接口不能通过运行中 PIO 指令注入破坏实时 SM。
- 完成内容：
  - `sync_io_capture_dma_produced_words()` 改为优先用 DMA `transfer_count` 推导 produced words，ring write index 只作为回退/镜像更新；恰好写满 8192 words 回到同一 index 时不再静默丢字。
  - `sync_io_enc_count_get_count()` 删除运行中 `pio_sm_exec(mov isr,x)` / `push` 注入，只消费已有 RX FIFO snapshot 并返回安全镜像，避免破坏 `enc_count.pio` 内部 ISR/OSR 解码流程。
  - `sync_io_model_sched` 的 `completion_ns[]` 改为 64 位累计 ns，长于 4.29 s 的 pulse schedule 不再因 32 位饱和提前标记完成。
  - `VDC_DOMAIN_RISK_REVIEW.md` 将 DMA 丢字、ISR 被踩、完成计数饱和更新为“已纠偏”。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816165320`，package CRC `0x9078EBED`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `python tools\docs_check\docs_check.py` 通过；保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 传输和 slot commit 完成，但 reboot 后脚本首次 `SYSTem:FW:BUILD?` 超时导致工具返回失败；后续单独复查确认 COM5/COM6 均已运行 build `20260816165320`，slot committed，`SYSTem:ERRor?` 为 `0,"No error"`。
  - COM5/COM6 只读 SCPI 复查通过：`SYSTem:SYNC:VDC:DCO?` 显示 DCO consumer valid 且 `last_error=0`，`SYSTem:SYNC:VDC:LOCK:READiness?` 当前仍因未启动 observation/self-test 而未锁定。
- 还需完成：
  - ENC_COUNT 精确 live count 仍需后续由 PIO 主动发布无扰动 snapshot 或迁移到 PCNT 基础件；本轮先关闭“查询破坏实时 SM”的 bug。
- 关联文件：
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/sync_io_mode_enc_count.c`
  - `components/sync_io/src/sync_io_model_sched.c`
  - `docs/vdc/VDC_DOMAIN_RISK_REVIEW.md`
- 下一步：
  - 进入共享 SM/owner 互斥、fire 假成功和非 capture DMA claim。

### VDC-TASK-20260817-002 - P2 timestamp and capture baseline risk closure

- 状态：完成代码、host 单元验证、构建、文档检查、COM5/COM6 OTA 和只读 SCPI 复查。
- 日期：2026-08-17
- 任务目标：
  - 按 `VDC_DOMAIN_RISK_REVIEW.md` 继续关闭会影响 DPLL timestamp 输入正确性的 P2 项。
  - 将已经纠偏的风险显式标为“已纠偏”，避免后续重复排查同一问题。
- 完成内容：
  - `sync_io_capture_latch_service_core1()` 的 capture word span 改为 64 位计算，`sample_hz==1` 时 `8 * sample_period_ns` 不再溢出；`sync_io` 内部定义本层 `SYNC_IO_CAPTURE_WORD_SAMPLES=8`，不反向依赖 VDC adapter。
  - 新增 `vdc_wrap_tracker_reanchor()`，发布 timestamp dictionary 时只重设低 32 位锚点，不清 `tick_hi64/wrap_count/backward_reject_count`，避免运行超过 4.29 s 后重新加载 dictionary 破坏扩展 tick。
  - `vdc_sync_io_capture_word_to_compact_observation()` 在 ambiguous word 上改为消费完整 word 的末样本作为下一 word 基线，避免半截基线制造伪边沿。
  - `VDC_DOMAIN_RISK_REVIEW.md` 增加风险状态列，已纠偏项改为“已纠偏”，未完成项保持“待纠偏/部分已纠偏/待文档化”。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816164242`，package CRC `0xE447BA21`。
  - `python tools\docs_check\docs_check.py` 通过；保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过；COM5/COM6 均启动并 commit 到 build `20260816164242`。
  - COM5/COM6 只读 SCPI 复查通过：`SYSTem:FW:BUILD?` 均为 `20260816164242`，`SYSTem:SYNC:VDC:DCO?` 显示 DCO consumer valid 且 `last_error=0`，`SYSTem:ERRor?` 均为 `0,"No error"`。
- 还需完成：
  - 继续处理待纠偏项：DMA 丢字、ENC_COUNT ISR 注入、completion 饱和、共享 SM 互斥、fire 假成功、非 capture DMA claim、RX self-test timeout。
- 关联文件：
  - `components/sync_io/src/sync_io.c`
  - `components/vdc_domain/inc/vdc_timestamp.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_domain/src/vdc_sync_io_adapter.c`
  - `components/vdc_domain/src/vdc_timestamp.c`
  - `tests/unit/test_vdc_domain.c`
  - `docs/vdc/VDC_DOMAIN_RISK_REVIEW.md`
- 下一步：
  - 进入 sync_io 剩余 P2 边界 bug：优先处理 DMA 丢字和共享 SM/owner 互斥。

### VDC-TASK-20260817-001 - P1 DPLL correctness risk closure

- 状态：完成代码、host 单元验证、构建、文档检查、COM5/COM6 OTA 和只读 SCPI 复查。
- 日期：2026-08-17
- 任务目标：
  - 按 `VDC_DOMAIN_RISK_REVIEW.md` 优先关闭 P1 风险：DCO 更新序列非单调、accepted 样本伪锁定、SEQ_STEP 输出宽度越界。
  - 保持 HAOFV 边界：`SyncDpllFB/VDC owner` 仍是 offset/rate/lock/DCO 的唯一 writer，`sync_io` 只提供硬实时 IO 基础能力，SCPI/脚本不写锁相状态。
- 完成内容：
  - `vdc_domain_update_clock_from_evidence()` 保存并恢复 `next_dco_seq`，accepted evidence 派生 DCO 后不再被 `vdc_domain_default_dco_control()` 重置为固定序号。
  - 新增 VDC lock acquisition reset helper；gate reject 和 servo outlier 清零连续 accepted streak、连续质量计数和频率估计基线，防止下一条好样本直接伪锁回 `LOCKED`。
  - `SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH` 绑定 `BOARD_SYNC_OUTPUT_PIN_COUNT`，当前产品输出组限制为 GPIO21-24 四线；gate 参数校验前移到 DMA/IRQ/SM 操作之前。
  - 单元测试补充 DCO evidence seq 单调性、reject 后 accepted streak reset、outlier 后不能立即 relock。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816163100`，package CRC `0x84D37534`。
  - `python tools\docs_check\docs_check.py` 通过；保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过；COM5/COM6 均启动并 commit 到 build `20260816163100`。
  - COM5/COM6 只读 SCPI 复查通过：`SYSTem:FW:BUILD?` 均为 `20260816163100`，`SYSTem:SYNC:VDC:DCO?` 显示 DCO consumer valid、accepted update 计数持续增长且 `last_error=0`，`SYSTem:SYNC:VDC:LOCK:READiness?` 当前仍因未启动 observation/self-test 而未锁定。
  - SD runtime log 读回通过：COM5 `/logs/run` 已有 `run_000001.log`..`run_000006.log`，COM6 已有 `run_000001.log`..`run_000005.log`；`MMEM:READ?` 可读最新日志。一次命令行引号错误和 `SYST:ERR?` 缩写误用产生了 `-101` 日志，已用 `*CLS` 清除，最终 `SYSTem:ERRor?` 为 `0,"No error"`。
- 还需完成：
  - 继续处理 `VDC_DOMAIN_RISK_REVIEW.md` 中 P2 风险，下一优先级是 wrap tracker 高 32 位重置、sync_io 边界 bug 和 manager/sync_io 覆盖缺口。
  - 启动 VDC observation/self-test 后继续复查 GPIO overlay 锁相日志中的 reject/recovery 行为。
- 关联文件：
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/sync_io/src/sync_io_mode_seq_step.c`
  - `tests/unit/test_vdc_domain.c`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_DOMAIN_RISK_REVIEW.md`
- 下一步：
  - 进入 P2 风险修复：优先处理 wrap tracker 高 32 位重置与 sync_io 边界 bug，然后再启动 VDC observation/self-test 进行锁相质量优化。

### VDC-TASK-20260816-039 - TDMA timestamp spine gates DPLL eligibility

- 状态：完成代码、host 单元验证和构建。
- 日期：2026-08-16
- 任务目标：
  - 将 DPLL eligible 判定前移到公共 `components/tdma` timestamp spine，避免 VDC/RefMem payload 或 adapter 私自把普通帧标记为锁相证据。
  - 修复 VDC payload parser 对 frame 内旧 timestamp flags 的信任风险，确保 DPLL 证据必须来自 TDMA 执行镜像。
- 完成内容：
  - `tdma_service_publish_exec_timestamp()` 增加公共门禁：只有 scheduled window 有效、window class 为 `VDC_OBSERVATION`、payload 为 `VDC_SYNC_SAMPLE/IDLE_BEACON`、timestamp source 为 `HARDWARE_TICK`、resolution `<=100 ns` 且非 diagnostic-only 时，才保留 `DPLL_ELIGIBLE`。
  - `vdc_tdma_payload_parse_frame()` 改为以 TDMA snapshot 的 core1 done timestamp 判定执行镜像是否存在；在 `require_dpll_eligible=true` 时，没有 TDMA 执行时间戳直接返回 `TIMESTAMP_NOT_ELIGIBLE`。
  - 单元测试增加 unwindowed hardware tick 负例：adapter 即使上报 `HARDWARE_TICK / 50 ns / DPLL_ELIGIBLE`，未进入 `VDC_OBSERVATION` window 时也会被 TDMA spine 清掉 eligible，VDC parser 必须拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816153413`，package CRC `0x06B015BC`。
- 还需完成：
  - 继续把产品路径的真实 PIO/DMA/core1 timestamp latch 接到公共 TDMA observation window，而不是依赖 GPIO4-7 overlay。
  - RefMem AUTO 同步仍需补 completion/retry/fence，防止 `WINDOW_MISSED` 导致静默丢帧。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/src/vdc_tdma_payload.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 在两板 COM5/COM6 上 OTA 新 build，复查 TDMA diagnostic self-test 和 RefMem AUTO 同步；随后推进真实 TDMA/VDC timestamp latch 闭环。

### VDC-TASK-20260816-038 - 100 ns GPIO overlay acquisition reaches VDC LOCKED

- 状态：完成代码、host 单元验证、构建、COM5/COM6 OTA 和双向 HIL。
- 日期：2026-08-16
- 任务目标：
  - 支持“大初始相位差先粗接纳，DPLL 后续慢慢追到细锁”的 acquisition 行为。
  - 把 GPIO4-7 bring-up 从 1 us coarse lock 推进到 `HARDWARE_TICK / 100 ns / DPLL_ELIGIBLE` 两板闭环。
  - 保持 HAOFV 边界：SCPI 只配置/查询 observer 和 self-test，不写 lock/offset/rate；`SyncDpllFB` 仍是 DCO/lock/quality 唯一 writer。
- 完成内容：
  - `sync_io_model_sched` 增加 ns 级 pulse schedule API，VDC self-test 使用 100 ns tick 预约模型输出。
  - `vdc_domain` 增加 acquisition admission：未锁定/重锁阶段允许整帧相位差进入 DPLL，锁定后用 DCO phase offset 平移 admission window；质量/锁定使用 DCO 校正后的 residual phase，不把初始大相位差冒充 fine lock。
  - `sync_io` capture latch 在 timestamp window armed 时进行边沿压缩，只向上层发布有观测边沿的 word；DMA 覆盖后重新同步边沿前态，不再永久清掉 capture timebase。
  - 新增只读维护查询 `REALtime:IO:MODel:PULSe:SCHEDule?` 和 `REALtime:IO:SAMPle:WINDow?`，用于 HIL 定位 PIO/DMA scheduler、timestamp window 和 capture timebase。
  - `tools/vdc_gpio_lock_validate.py` 固化 100 ns 验证、TX scheduler 状态和 RX timestamp window 证据输出。
- 验证结果：
  - `python -m py_compile tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py` 通过。
  - `tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816150506`，package CRC `0x1DC450F4`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816150506`。
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --expected-build 20260816150506 --poll-timeout 90 --pulse-count 4096 --pulse-high-ns 1000` 通过：`accepted=63->113 observer_accepted=0->47 state=LOCKED source=HARDWARE_TICK resolution_ns=100 flags=DPLL_ELIGIBLE gate=PASS`。
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM6 --port-y COM5 --name-x COM6 --name-y COM5 --expected-build 20260816150506 --poll-timeout 90 --pulse-count 4096 --pulse-high-ns 1000 --output-index 2 --observed-mask 4` 通过：`accepted=0->82 observer_accepted=0->81 state=LOCKED source=HARDWARE_TICK resolution_ns=100 flags=DPLL_ELIGIBLE gate=PASS`。
- 还需完成：
  - 当前 fine lock 仍依赖 GPIO4-7 调试 overlay；产品路径必须迁移到 GPIO16-24 TDMA 通讯帧 timestamp spine。
  - 已将默认 bring-up acceptance 收敛为 1 us，并在 acquisition 阶段使用受限大步进 phase slew 加快粗拉入；后续仍需在真实 TDMA timestamp path 上调 DPLL 环路参数并发布 lock_time、rms/peak offset、outlier 比例。
  - `lock_quality_tier` 已改为由连续 coarse/debug/fine 样本晋级，历史 `max_abs_offset_ns` 只保留为诊断峰值；产品 `HEALTHY/RUN` 仍必须等待连续 100 ns fine 样本。
  - `HEALTHY/RUN` 仍必须只消费 fine tier，不允许把 GPIO overlay bring-up 直接当产品同步依据。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/sync_io_model_sched.c`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`
  - `tests/unit/test_vdc_domain.c`
  - `tools/vdc_gpio_lock_validate/vdc_gpio_lock_validate.py`
- 下一步：
  - 把相同 acquisition/residual/DCO 机制迁移到公共 TDMA payload timestamp，逐步减少 GPIO4-7 overlay 对锁相验证的依赖。

### VDC-TASK-20260816-037 - Core1 consumes stable DCO mirror

- 状态：完成代码、host 单元验证、构建、COM5/COM6 OTA 和 HIL。
- 日期：2026-08-16
- 任务目标：
  - 将 `VdcDcoControl` 从 domain snapshot 字段推进为 core1 实时循环可验证的只读消费契约。
  - 保持 HAOFV 边界：`SyncDpllFB` 仍是 offset/rate/lock/DCO 的唯一 writer，core1 只读并镜像 DCO，SCPI 只读查询镜像，不写 DCO。
- 完成内容：
  - `vdc_dpll_manager` 增加 `vdc_dpll_manager_dco_consumer_status_t`，在 core1 DPLL service 中读取 `vdc_domain` snapshot，调用 `vdc_domain_dco_control_validate()` 验证 DCO contract。
  - DCO consumer mirror 记录 `valid/service_count/accepted_update_count/unchanged_count/invalid_count/last_error/last_dco_update_seq/source_model_seq/lock_state/phase_offset_ns/period_adjust_ppb/base tick/base vdc/schedule CRC/servo CRC`。
  - 新增只读 SCPI `SYSTem:SYNC:VDC:DCO?`，用于调试上位机查询 core1 已消费的 DCO 镜像。
  - HIL 复查发现 rejected diagnostic/self-test 后 DPLL 会回到 `CHECKING`，但 DCO `lock_state` 仍停留在 `LOCKED`；已在 `vdc_domain` 内增加 DCO lock_state 同步 helper，并用单元测试覆盖 rejected sample 后 DCO 也回到 `CHECKING`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816134351`，package CRC `0xB9EAD5C4`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816134351`。
  - `python tools\scpi_query\scpi_query.py COM5 "SYSTem:FW:BUILD?" "SYSTem:SYNC:VDC:DCO?" "SYSTem:SYNC:VDC:DPLL:STATus?"` 通过：初始 DCO/DPLL 均为 `CHECKING`，`valid=1 invalid_count=0 last_error=0`。
  - `python tools\scpi_query\scpi_query.py COM6 "SYSTem:FW:BUILD?" "SYSTem:SYNC:VDC:DCO?" "SYSTem:SYNC:VDC:DPLL:STATus?"` 通过：初始 DCO/DPLL 均为 `CHECKING`，`valid=1 invalid_count=0 last_error=0`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816134351 --poll-timeout 5` 通过，diagnostic-only evidence 仍被 gate 拒绝。
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --expected-build 20260816134351 --poll-timeout 30 --pulse-count 2200 --sample-period-ns 1000 --pulse-high-ns 2000 --output-index 2 --observed-mask 4 --reverse` 通过：X->Y `accepted=0->11`，Y->X `accepted=0->9`，两向均 `state=LOCKED source=HARDWARE_TICK resolution_ns=1000 flags=DPLL_ELIGIBLE gate=PASS`。
  - 锁相脚本释放后再次查询 `SYSTem:SYNC:VDC:DCO?` 和 `SYSTem:SYNC:VDC:DPLL:STATus?`：COM5/COM6 均显示 DCO `lock_state=CHECKING` 与 DPLL `state=CHECKING` 一致，未继续发布 stale `LOCKED` DCO。
- 还需完成：
  - core1 当前只验证并镜像 DCO，还没有用 DCO 装载 PIO `FIRE_LOAD` 或同步输出。
  - DCO half-update/stale 拒绝目前依赖 critical snapshot 和 seq 计数；后续跨核共享区需要 seqlock/double-buffer contract。
  - 当前仍是 GPIO4-7 bring-up coarse lock；产品路径仍需要 TDMA 通讯 timestamp evidence 和 100 ns fine tier。
- 关联文件：
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `components/vdc_domain/src/vdc_domain.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 将 core1 DCO mirror 接到最小 PIO 同步输出或 FIRE_LOAD 装载路径，并继续推进 TDMA 通讯 timestamp evidence 替代 GPIO4-7 调试观测。

### VDC-TASK-20260816-036 - GPIO4-7 overlay validates coarse VDC lock

- 状态：完成代码、host 单元验证、构建、COM5/COM6 OTA 和双向 HIL。
- 日期：2026-08-16
- 任务目标：
  - 在不占用 GPIO16-24 TDMA 通讯环路的前提下，使用 GPIO4-7 调试 overlay 验证 VDC/DPLL 的硬件 timestamp 入口和粗锁定路径。
  - 保持 HAOFV 边界：GPIO4-7 只作为最小系统 bring-up 观测线，产品路径后续必须迁移到 TDMA 通讯帧/时间戳证据；`HEALTHY/RUN` 仍要求 100 ns fine tier。
- 完成内容：
  - `PROJECT_SYNC_IO_INPUT_BASE_PIN` 默认切换到 GPIO4，使 `sync_io` capture 观察调试 overlay，而不是复用 GPIO16-24 TDMA 物理通讯线。
  - `sync_io` DMA produced 计数改为根据 DMA write address 计算，capture ring 扩大到 8192 words，latch ring 扩大到 128，service batch 扩大到 128，避免高频脉冲下 ring 进度被 `transfer_count` 误判。
  - timestamp window 匹配从“capture word 必须完全包含在 window 内”调整为 word/window overlap，适配当前 word-level PIO capture 粒度。
  - VDC self-test TX 侧通过 `sync_io_model_pulse_schedule_arm()` 在 observation window 附近预约 GPIO4-7 pulse train；RX 侧由 observer/capture 生成 `HARDWARE_TICK / 1000 ns / DPLL_ELIGIBLE` evidence 并进入 DPLL admission。
  - DPLL bring-up admission 临时允许 `timestamp_resolution_ns <= 1000` 形成 coarse/debug lock；正式 `HEALTHY/RUN` 质量门禁不放宽，后续仍必须收敛到 100 ns fine tier。
- 验证结果：
  - `python -m py_compile tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260816132834`，package CRC `0x4EC7EEB0`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816132834`。
  - `python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM5 --port-y COM6 --line-settle 0.1` 通过：`COM5.GPIO4/5/7 -> COM6.GPIO4/5/7`，`COM6.GPIO6 -> COM5.GPIO6`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816132834 --poll-timeout 5` 通过：TDMA diagnostic self-test 仍以 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY` 被 gate 拒绝，不冒充锁相信号。
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --expected-build 20260816132834 --poll-timeout 30 --pulse-count 2200 --sample-period-ns 1000 --pulse-high-ns 2000` 通过：X->Y `accepted=0->7 observer_accepted=0->7 state=LOCKED source=HARDWARE_TICK resolution_ns=1000 flags=DPLL_ELIGIBLE gate=PASS`。
  - `python tools\vdc_gpio_lock_validate\vdc_gpio_lock_validate.py --port-x COM5 --port-y COM6 --expected-build 20260816132834 --poll-timeout 30 --pulse-count 2200 --sample-period-ns 1000 --pulse-high-ns 2000 --output-index 2 --observed-mask 4 --reverse` 通过：X->Y `accepted=14->30 observer_accepted=0->15`，Y->X `accepted=0->5 observer_accepted=0->4`，两向均 `state=LOCKED source=HARDWARE_TICK resolution_ns=1000 flags=DPLL_ELIGIBLE gate=PASS`。
- 还需完成：
  - 当前是 1 us resolution coarse lock；产品 `HEALTHY/RUN` 仍需要 100 ns fine lock tier。
  - GPIO4-7 只是最小系统调试观测，最终应把 lock evidence 迁移到 TDMA 通讯帧/时间戳 spine。
  - VDC 诊断循环后续应由内部 AO/FB 持续运行并发布 FIFO/vector 镜像，SCPI 只读快照或配置启停，不参与实时 lock/offset/rate。
- 关联文件：
  - `CMakeLists.txt`
  - `boards/rp2350_trig/inc/board_config.h`
  - `components/sync_io/src/sync_io.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tools/vdc_gpio_lock_validate/vdc_gpio_lock_validate.py`
- 下一步：
  - 先保持 GPIO4-7 bring-up 证据作为最小硬件闭环，再推进 TDMA 通讯 timestamp evidence、DCO snapshot core1 消费和 100 ns fine tier 收敛。

### VDC-TASK-20260816-035 - SyncIO capture timebase aligns with TDMA epoch

- 状态：完成代码、host 单元验证、构建、COM5/COM6 OTA 和 HIL。
- 日期：2026-08-16
- 任务目标：
  - 让 `sync_io` PIO/DMA capture word 的时间基准与 TDMA/VDC window planner 使用同一个 boot-time ns epoch。
  - 保持 HAOFV 边界：`sync_io` 只产出 timestamp evidence，VDC gate 决定是否接纳，SCPI 不写 lock/offset/rate。
- 完成内容：
  - `sync_io_start_capture()` 的 `capture_timebase_start_ns` 改为 common boot-time ns；板上使用 `time_us_64()*1000`，host fallback 保留 latch timer tick 转换。
  - `sync_io_capture_time_now_ns()` 返回同一 common boot-time ns，避免被 reset 的 latch timer epoch 与 TDMA schedule epoch 分裂。
  - TDMA scheduled intent 的 near-window arm-ahead 从 250 us 调整到 2 ms，保证当前 1 ms bring-up schedule 下 core1 service 能稳定驻留到 10 us observation window；这仍是基础件策略，后续应由真正 core1 scheduler/timer 替代长驻留。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816124128`，package CRC `0x8A060D1F`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816124128`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816124128 --poll-timeout 5` 通过：COM5/COM6 均 `ready=0->1 payload=1 source=1 resolution_ns=1000 flags=0x1 vdc_gate=9 lock_tier=0 lock_accept_ns=10000`。
- 还需完成：
  - 增加 RX/PIO capture HIL，验证 `SYSTem:SYNC:VDC:OBServer:TDMA` 周期窗口内真实 capture word 能带 `HARDWARE_TICK` 进入 VDC observer。
  - 将 2 ms arm-ahead 驻留替换为 TDMA core1 scheduler/timer 或 PIO/DMA deadline 驱动，减少 core1 忙等。
- 关联文件：
  - `components/sync_io/src/sync_io.c`
  - `components/tdma/src/tdma_service.c`
- 下一步：
  - 编写并固化 VDC RX capture HIL 脚本，验证 GPIO4-7 overlay 或已连接观测输入上的 observation evidence；然后进入 DPLL accepted sample 和 lock 收敛闭环。

### VDC-TASK-20260816-034 - VDC self-test uses scheduled TDMA window

- 状态：完成代码、host 单元验证、构建、COM5/COM6 OTA 和 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复 VDC self-test TX intent 构造了 `VDC_OBSERVATION_WINDOW` 但没有让公共 TDMA service 按窗口执行的问题。
  - 让 self-test 继续只作为 diagnostic evidence，但其 frame 执行必须受 TDMA active schedule 的 wait/late/miss 约束。
- 完成内容：
  - `vdc_dpll_manager_start_observation_self_test()` 提交 `VDC_SYNC_SAMPLE` short frame 时将 `scheduled_window_valid` 置为 1，并携带 window class、schedule CRC、window start/end 和 guard start/end。
  - `tdma_service_core1_service()` 增加 near-window arm-ahead 驻留：距离 guard 很远时保持 `WAITING_FOR_WINDOW`，进入可控提前量后由 TDMA core1 service 等到 window 再执行 payload，避免依赖主循环碰巧命中 10 us 窗口。
  - `vdc_dpll_manager_start_observation_self_test()` 和 `SYSTem:SYNC:VDC:OBServer:TDMA` 改用 TDMA 执行时基规划窗口；板上为 `time_us_64()*1000`，`sync_io_capture_time_now_ns()` 只保留为 capture latch 证据时基，不能作为 TDMA 调度 now。
  - `test_vdc_tdma_payload_mounts_on_common_tdma()` 增加 windowed TDMA intent 覆盖：未到 guard/window 时 `tdma_service_core1_service()` 必须返回 `WAITING_FOR_WINDOW` 且不能提前 transmit，进入窗口后必须完成 `FRAME_READY`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816123106`，package CRC `0xDABA31B6`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816123106`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816123106 --poll-timeout 5` 通过：COM5/COM6 均 `ready=0->1 payload=1 source=1 resolution_ns=1000 flags=0x1 vdc_gate=9 lock_tier=0 lock_accept_ns=10000`。
- 还需完成：
  - 继续接入真正 PIO edge latch，把 observation window 内样本升级为 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE`。
  - TDMA completion/retry/fence 仍需完善，避免 RefMem/VDC payload 因一次 missed/timeout 静默丢失。
- 关联文件：
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 进入 P0.2/P0.3 的真实 timestamp latch、completion/retry/fence 和 DPLL accepted sample 闭环。

### VDC-TASK-20260816-033 - DPLL lock quality tiers for bring-up

- 状态：完成代码、SCPI 字段、单元测试、构建、COM5/COM6 OTA 和 HIL。
- 日期：2026-08-16
- 任务目标：
  - 初步验证阶段允许 DPLL 锁定接纳阈值按 100 ns、1 us、10 us 三档放宽。
  - 保持 HAOFV 语义：调试粗锁定不能冒充产品级 `HEALTHY/RUN` 质量门禁。
- 完成内容：
  - `vdc_servo_profile_t` 增加 `debug_lock_threshold_ns`、`coarse_lock_threshold_ns` 和 `lock_acceptance_threshold_ns`。
  - `vdc_quality_table_t` 增加 `lock_quality_tier` 以及当前 fine/debug/coarse/acceptance 阈值快照。
  - 默认 profile 保持 100 ns fine 目标，同时把 bring-up 接纳阈值设为 10 us；`LOCKED` 可用于观察收敛，`HEALTHY` 仍必须达到 `FINE_100NS`。
  - `READ:SYNC:QUALity?` 尾部追加 `lock_quality_tier,fine_lock_threshold_ns,debug_lock_threshold_ns,coarse_lock_threshold_ns,lock_acceptance_threshold_ns`。
  - HIL 验证脚本更新字段数，并确认 diagnostic TDMA self-test 不产生 lock tier。
- 验证结果：
  - `python -m py_compile tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816120839`，package CRC `0xC3F91EE3`。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件名 warning。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816120839`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816120839` 通过：COM5/COM6 均为 `source=1 resolution_ns=1000 flags=0x1 vdc_gate=9 lock_tier=0 lock_accept_ns=10000`。
- 还需完成：
  - 后续 SCPI profile 配置需要能在维护态选择 `COARSE_10US / DEBUG_1US / FINE_100NS` 接纳阈值；当前先落基础字段和默认 bring-up profile。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`
- 下一步：
  - 运行 VDC 单元测试、主机构建、固件构建和 COM5/COM6 HIL，验证字段兼容和 self-test gate 语义。

### VDC-TASK-20260816-032 - VDC TDMA self-test enters gate

- 状态：完成代码、脚本增强、构建和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 让 VDC self-test 不只停在 TDMA service intent 完成，还要把 `VDC_SYNC_SAMPLE` frame 的 timestamp evidence 送入 VDC gate。
  - 保持证据诚实：当前 TDMA self-test 仍是 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`，只能被 VDC gate 拒绝，不能产生 lock。
- 完成内容：
  - `vdc_dpll_manager` 保存 self-test 生成的 `VdcTDMATimestampEvidence`，在公共 TDMA service 完成对应 intent 后提交给 `vdc_domain_submit_tdma_evidence()`。
  - VDC gate 按 `TIMESTAMP_NOT_ELIGIBLE` 拒绝该 diagnostic evidence，并更新 quality/rejected 计数。
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py` 增加 `READ:SYNC:QUALity?` 验证，要求 rejected sample 增长、`last_reject_code=9`、timestamp resolution 为 1000 ns。
- 验证结果：
  - `python -m py_compile tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816115630`，package CRC `0xF14C9338`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816115630`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816115630` 通过：COM5/COM6 均为 `ready=0->1 intent=1 completed=1 payload=1 source=1 resolution_ns=1000 flags=0x1 vdc_rejected=0->1 vdc_gate=9`。
- 还需完成：
  - self-test 已闭合到 VDC gate，但仍不是 DPLL lock evidence；下一步必须接真正 PIO edge latch / timestamp spine。
  - 将 TDMA service 的硬件 timestamp source 从软件诊断升级为 observation window 内的 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE`。
- 关联文件：
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`
- 下一步：
  - 继续 P0.2：把 TDMA observation window 的真实 edge latch 接入 `VdcTDMATimestampEvidence`，使 gate 能从拒绝 diagnostic evidence 推进到接受硬件样本。

### VDC-TASK-20260816-031 - VDC self-test mounted on TDMA service

- 状态：完成代码、脚本语义纠偏、host/build 验证和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 保持 VDC 由公共 TDMA 产生和维护，同时允许 VDC self-test 事务并行运行。
  - 移除 VDC self-test 对 SyncIO 主输出组 pulse train 的直接占用，避免把 GPIO16-24 TDMA 通讯环路误当业务模型 IO。
  - self-test 只能证明 TDMA scheduler / VDC payload / window evidence 通路，不得伪造 DPLL lock。
- 完成内容：
  - `vdc_dpll_manager` 将内部 `tdma_service` 绑定为 VDC self-test transport，并在 core1 realtime loop 中服务。
  - `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest role=1` 改为构造 `VDC_SYNC_SAMPLE` short frame 并提交到公共 TDMA service；`role=2/3` 仍可同时配置 RX observer/capture。
  - self-test TX evidence 固定为 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`，可通过 `SYSTem:SYNC:VDC:TDMA:STATus?` 验证 intent 完成、payload class 和 timestamp flags，但不能让 DPLL gate 通过。
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py` 不再探测或驱动 `REALtime:IO:OUTPut` 主输出线，改为验证每块板的 VDC TDMA self-test intent。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816114847`，package CRC `0x9445A68E`。
  - `python tools\ota_multi_update\ota_multi_update.py build-rtos-multicore-smoke\RP2350_TRIG_UPDATE.pkg --ports COM5 COM6 --max-workers 2 --verbose` 通过，COM5/COM6 均 commit 到 `20260816114847`。
  - `python tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816114847` 通过：COM5 `ready=0->1 intent=1 completed=1 payload=1 source=1 resolution_ns=1000 flags=0x1`，COM6 同样通过。
- 还需完成：
  - 接入真正 PIO edge latch，把 TDMA observation window 内的 timestamp 升级为 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE`。
  - 将 VDC self-test 的 RX 观测证据与 TDMA frame/result evidence 合并成板端报告，不使用 host 耗时作为锁相证据。
- 关联文件：
  - `application/src/app.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`
- 下一步：
  - 运行 VDC host tests、全量 host unit tests、固件构建和 COM5/COM6 self-test 回归；通过后进入 edge latch / DPLL eligible evidence。

### VDC-TASK-20260816-030 - DPLL servo minimum loop

- 状态：完成代码、host/build 验证；实板 DPLL lock 仍待 TDMA observation edge latch 闭环。
- 日期：2026-08-16
- 任务目标：
  - 将 VDC DPLL 从 accepted sample 直接写 offset/rate 的首版推进为可门禁、可限幅、可测试的最小 servo。
  - 保持 HAOFV 边界：`SyncDpllFB` 语义仍在 VDC owner 内部执行，SCPI/TDMA/SYNC_IO 只提供配置、payload、timestamp fact 或 snapshot，不直接写 lock/offset/rate。
- 完成内容：
  - 新增 `VDC_DOMAIN_GATE_SERVO_OUTLIER`，accepted path 在进入 DPLL 统计前按 `outlier_threshold_ns` 拒绝异常 phase sample，并把拒绝码写入 quality/evidence。
  - DPLL phase correction 改为使用 `kp_q16` 计算目标 offset，并按 `step_threshold_ns` slew；首个样本在 `first_step_threshold_ns` 内允许 coarse step。
  - DPLL rate correction 保留 sample period frequency estimator；达到 `lock_sample_count` 后，使用 `ki_q16` 将持续 phase error 转为 rate pull，统一受 `sanity_freq_limit_ppb` 限幅。
  - `VdcClockModel` 和 `VdcDcoControl` 继续作为 DPLL 输出快照，`VdcErrorBudget.freq_offset_ppb/freq_skew_ppb` 记录实际应用的 rate correction。
  - 单元测试新增 servo outlier、phase slew 和锁定后 rate pull 覆盖，同时保留公共 TDMA hardware timestamp evidence 进入 DPLL lock 的 host 仿真路径。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，18/18。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816112835`，package CRC `0x90764B0A`。
- 还需完成：
  - 真实 COM5/COM6 板端 DPLL lock 仍不能只靠 host 仿真确认；VDC 本体继续基于 TDMA observation window，GPIO4-7 只作为模型/业务 overlay，正式锁相仍需要真正 TDMA edge latch。
  - core1 读取稳定 `VdcDcoControl` 并执行 phase pull / FIRE_LOAD 的路径仍未接入。
  - HOLDOVER / RELOCK / servo reset policy 仍未实现。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 先完成 VDC self-test 到公共 TDMA service 的资源边界纠偏；随后接入真正 TDMA edge latch，再验证 accepted hardware sample 是否能在两板上驱动 DPLL lock。

### VDC-TASK-20260816-029 - VDC payload mounted on common TDMA

- 状态：完成代码、host/build 验证；COM5/COM6 已完成 RefMem AUTO 回归。
- 日期：2026-08-16
- 任务目标：
  - 将 VDC 从私有 TDMA 语义中收敛到 `components/tdma` 公共基础件。
  - VDC 只负责 `VDC_SYNC_SAMPLE` / `IDLE_BEACON` payload 编解码、门禁和 timestamp evidence，不拥有第二套 scheduler/transport。
- 完成内容：
  - `tdma_service` 增加 `frame_class` / `payload_class` intent 和 snapshot 字段，并在 submit 阶段检查 payload registry。
  - 新增 `vdc_tdma_payload.h/.c`：注册 VDC sync sample 与 idle beacon short-frame payload；使用固定小端布局编码 `VdcTdmaFrameEnvelope`，校验 frame CRC、payload CRC、schedule/window/payload gate。
  - `vdc_tdma_payload_parse_frame()` 将公共 TDMA snapshot 的执行时间戳映射回 VDC evidence；当前公共 TDMA 仍声明 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`，因此只能作为质量/诊断证据，不能进入 100 ns DPLL lock gate。
  - `components/vdc_domain/CMakeLists.txt`、主固件 CMake 和 VDC host 单测脚本已接入新源。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过，覆盖 VDC payload 注册、公共 TDMA submit、未注册 payload 拒绝、CRC parse 和 diagnostic timestamp DPLL gate reject。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过，确认 RefMem payload 标准注册未被破坏。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260816103607`，package CRC `0x5B3FC367`。
  - COM5/COM6 多线程 OTA 到 `20260816103607` 通过。
  - `python tools\refmem_node_load_auto_hil_validate\refmem_node_load_auto_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_auto_tdma_payload_regression_retry --sync-timeout-s 15` 通过，验证 RefMem 作为公共 TDMA payload 后仍能完成 A->B、B->A 双向两节点 AUTO 同步和 maintenance。
- 还需完成：
  - 将公共 TDMA 的 timestamp source 从软件诊断升级为 core1 PIO/DMA hardware latch，形成 `HARDWARE_TICK + DPLL_ELIGIBLE + <=100 ns` 样本。
  - P0 优先为 TDMA/AUTO 单发窗口中的 `WINDOW_MISSED` 增加 ACK/重发或 fence completion 策略，避免后续把偶发丢帧当成同步成功；VDC DPLL 不能建立在不可证明 completion 的 TDMA 维护循环上。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/inc/vdc_tdma_payload.h`
  - `components/vdc_domain/src/vdc_tdma_payload.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 继续把 VDC observation window 的真实 PIO/DMA/core1 timestamp latch 接到公共 TDMA frame，驱动 DPLL offset/rate 收敛。

### VDC-TASK-20260816-028 - DMA capture window budget correction

- 状态：完成代码、构建、COM5 启动验证；COM6 硬件恢复后待双向 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复 10 MHz self-test capture 的 core1 无界 DMA 消费，防止 realtime loop 长时间不返回并造成 USB/SCPI 假死。
  - 保持 timestamp 证据诚实：只丢弃 TDMA observation window 外的采样 backlog，窗口内 word 仍完整保留给 observer/DPLL gate；不得伪造 `DPLL_ELIGIBLE`。
- 完成内容：
  - `sync_io` 的 capture FIFO 改由 DMA channel 3 环形缓存搬运；core1 每次最多处理 128 个 word。
  - timestamp window 已 arm 时，core1 根据 capture timebase 和 DMA word sequence 跳到下一个可能的 observation window，只保留窗口内 raw word；窗口外数据不进入 latch ring，也不计为 DMA overflow。
  - `VDC_DPLL_MANAGER_SYNC_IO_MAX_BATCH_WORDS` 从 8 提升到 32，确保一个 10 us / 100 ns observation window 的完整 word 集能在 VDC task 一次 service 中消费。
  - 修复“第一 observation window 尚未到达”被误判为窗口过期的边界条件；`start_delay_ns` 和 16 字段 self-test status 已同步到三份 SCPI 文档。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke` 通过，最终 build id `20260816065050`，OTA package CRC `0x65948FE3`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `python -m py_compile tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 通过。
  - COM5 已 OTA/boot/commit 到前置验证 build `20260816063518`；`SYST:LOOP:STAT? -> 1,281272,3557,260133`，`SYST:ERR? -> 0,"No error"`。TX-only self-test 返回 `1`，PIO/DMA pulse schedule 成功 arm，错误队列为空。最终双板闭环前 COM5 也需更新到 `20260816065050`。
  - COM6 在旧版 10 MHz DMA self-test 后 USB CDC 句柄不可用，OTA 传输超时，且 `picotool load -f` 无法将对应 USB device 强制重启入 BOOTSEL；需物理复位或重新插拔后再 OTA 到 `20260816065050`。
- 还需完成：
  - COM6 恢复后 OTA/commit 两板，并运行 `tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py --port-a COM5 --port-b COM6 --expected-build 20260816065050`。
  - 验收必须同时满足 X->Y、Y->X `accepted_count` 增长、gate=0、source=2、resolution<=100 ns、`DPLL_ELIGIBLE=1`、`DIAGNOSTIC_ONLY=0` 和两板 core loop 持续增长。
- 关联文件：
  - `components/sync_io/src/sync_io.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`
- 下一步：
  - 恢复 COM6 后先验证 DMA budget 修复不再造成 core1/USB 假死，再完成两板双向 VDC timestamp closure。

### VDC-TASK-20260816-027 - TDMA observer self-test edge foundation

- 状态：完成代码和本地构建；COM5/COM6 HIL 待烧录后执行。
- 日期：2026-08-16
- 任务目标：
  - 不再依赖 PC 串口赶 `VDC_OBSERVATION_WINDOW`，建立固件内部 RX observation + TX PIO pulse train 的两板 bring-up 基础。
  - 保持 HAOFV 边界：SCPI 只启动维护 self-test；`vdc_dpll_manager` 规划 active TDMA window；`sync_io` 装载 capture/window 和 PIO/DMA 输出；VDC gate 仍按 dictionary、CRC、timestamp flags 裁决。
- 完成内容：
  - `sync_io` timestamp window 支持按 TDMA period 重复匹配，并在 latched word 中记录命中的 `matched_window_start_ns`，避免后续周期样本继续使用第一周期 expected window。
  - `sync_io_model_sched` 的 PIO/DMA scheduled pulse primitive 增加主输出组入口 `sync_io_output_pulse_schedule_arm()`；旧 debug model 入口保留。
  - `vdc_dpll_manager` 新增 observation self-test API，支持 RX 侧启动周期性 TDMA observer + capture，TX 侧用主输出组发 PIO pulse train。
  - 新增 `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` / `SELFtest?` 维护命令；该命令不写 lock/offset/rate，只启动 bring-up 事务和读取状态。
  - 新增 `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`，自动检测两板线序，并分别验证 X->Y、Y->X 的 RX accepted sample、gate pass 和 `DPLL_ELIGIBLE` timestamp metadata。
- 验证结果：
  - `python -m py_compile tools\vdc_tdma_selftest_validate\vdc_tdma_selftest_validate.py` 通过。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=132，包含 self-test 命令。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，当前 build id `20260816052515`，OTA package CRC `0x59359177`。
- 还需完成：
  - 烧录 COM5/COM6 并运行 `vdc_tdma_selftest_validate.py`，确认真实两板线上的 accepted sample 和 gate pass。
  - 若 accepted sample 仍不稳定，下一步应调整 self-test pulse density、capture sample period 或补真正 PIO edge timestamp latch；不得用脚本伪造 accepted evidence。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/sync_io_model_sched.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tools/vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`
- 下一步：
  - COM5/COM6 OTA 后执行两板 HIL；通过后再推进 `SyncDpllFB` servo/outlier 和 core1 DCO snapshot 消费。

### VDC-TASK-20260816-026 - SyncIO timestamp window gate for VDC observation

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 在 `sync_io` 内增加 timestamp window 基础件，使 VDC/TDMA 可以 arm 一个绝对 ns observation window。
  - 让 `OBServer:TDMA` 的配置逻辑下沉到 `vdc_dpll_manager`，SCPI 只提交维护意图，不直接拼跨域细节。
  - 修复 VDC plan 与 capture latch 使用不同时间基的问题，保证后续 eligible 样本有机会通过 window gate。
- 完成内容：
  - `sync_io` 新增 `sync_io_capture_time_now_ns()`，返回 capture latch timer1/CLK_SYS 的当前 ns 时间。
  - `sync_io` 新增 `sync_io_capture_arm_timestamp_window()` / `sync_io_capture_disarm_timestamp_window()`；只有整个 capture word 落在已 arm window 内，且 sample period/observed mask 匹配时，word 才可能携带 `DPLL_ELIGIBLE`，否则继续 `DIAGNOSTIC_ONLY`。
  - `vdc_dpll_manager_configure_sync_io_observer_tdma()` 负责读取 capture time、规划 `VDC_OBSERVATION_WINDOW`、配置 observer、arm timestamp window，并在关闭 observer 时 disarm window。
  - `vdc_dpll_manager_sync_io_observer_service()` 对 eligible word 保留实际 latch base time；只有非 eligible 诊断 bring-up 才使用 TDMA window base 辅助观察。
  - `scpi_cmd_sync_vdc_observer_tdma()` 简化为调用 manager API，不再直接访问 VDC plan 或 SyncIO window。
- 验证结果：
  - `python -m py_compile tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py` 通过。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=130。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816051125`，OTA package CRC `0x618FCB41`。
  - COM5/COM6 均 OTA/commit 到 build `20260816051125`。
  - `python tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py COM5 COM6 --expected-build 20260816051125 --out-dir build-rtos-multicore-smoke\vdc_lock_readiness_validate_20260816051125_window` 通过：COM5/COM6 均为 `reason=5,submitted=1,accepted=0,rejected=1,gate=9,source=2,resolution_ns=4,flags=1`。
- 还需完成：
  - 两板最小闭环不能依赖 PC 串口赶 10us observation window；下一步需要固件内部按 TDMA window 预约测试边沿和采样。
  - 真实 PIO edge latch 到位后，验证 `DPLL_ELIGIBLE` 样本 accepted_count 增长、gate pass、sample/frame CRC 记录正常。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
- 下一步：
  - 做 `TDMA observation self-test edge` 最小基础件：发送板预约 window 内输出，接收板 window 内采样，SCPI 只读取两板 result。

### VDC-TASK-20260816-025 - TDMA observation window observer bring-up

- 状态：完成代码、文档、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 将 VDC observer 的最小实例配置从手工 expected window/base 收敛到 active `VdcTdmaScheduleProfile` 的 `VDC_OBSERVATION_WINDOW`。
  - 保持 HAOFV 边界：SCPI 只请求 VDC manager 按 active schedule 配置 observer，不启动 capture，不提交样本，不写 DPLL lock/offset/rate。
  - 保持时间戳诚实：TDMA window base 不能把 core1 drain FIFO timestamp 提升为 DPLL eligible。
- 完成内容：
  - 新增 `SYSTem:SYNC:VDC:OBServer:TDMA [enabled],[initial_sample_mask],[sample_period_ns],[frame_crc32]`。
  - `OBServer:TDMA` 通过 `vdc_dpll_manager_plan_tdma_window(VDC_OBSERVATION_WINDOW)` 获取 active observation window，配置 observer 的 `expected_window_start_ns`、`next_base_time_l32_ns` 和 frame CRC。
  - `vdc_dpll_manager` 新增 `VDC_DPLL_MANAGER_OBSERVER_QUALITY_TDMA_WINDOW_BASE`，adapter decode 在该标志下使用 active TDMA window start 作为 compact observation base time。
  - `sync_io` latch 输出的 `timestamp_source/resolution/flags` 仍作为唯一 timestamp metadata 来源；当前仍为 `HARDWARE_TICK / 4 ns / DIAGNOSTIC_ONLY`，因此 readiness 必须继续停在 `TIMESTAMP_NOT_ELIGIBLE`。
  - `vdc_lock_readiness_validate.py` 改为使用 `OBServer:TDMA`，并检查 expected window 与 `quality_flags bit31`。
- 验证结果：
  - `python -m py_compile tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py` 通过。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=130，`SYSTem:SYNC:VDC:OBServer:TDMA -> 1`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816050037`，OTA package CRC `0xDFBCAF42`。
  - COM5 OTA/commit 到 build `20260816050037`，`ota_boot_commit.py` 通过。
  - COM6 OTA 到 build `20260816050037`，`ota_boot_commit.py` 被启动日志串扰误判；独立查询确认 `SYST:FW:BUILD? -> "20260816050037"`、`SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`、`SYST:ERR? -> 0,"No error"`。
  - `python tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py COM5 COM6 --expected-build 20260816050037 --out-dir build-rtos-multicore-smoke\vdc_lock_readiness_validate_20260816050037_tdma` 通过：COM5/COM6 均为 `reason=5,submitted=1,accepted=0,rejected=1,gate=9,source=2,resolution_ns=4,flags=1`。
- 还需完成：
  - P0.1 后半段：让 PIO/DMA/IRQ/core1 在 `VDC_OBSERVATION_WINDOW` 内形成真实 edge latch，不再依赖 core1 drain FIFO timestamp。
  - P0.2：样本 metadata 满足 `HARDWARE_TICK + DPLL_ELIGIBLE + resolution<=100ns + !DIAGNOSTIC_ONLY` 后，验证 `accepted_count` 增长且 gate pass。
- 关联文件：
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tools/vdc_lock_readiness_validate/vdc_lock_readiness_validate.py`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
- 下一步：
  - 实现真正的 observation-window edge latch，优先利用已存在的 TDMA 计划和 `BOARD_SYNC_TIMESTAMP_SM` 预留资源，避免新增旁路同步机制。

### VDC-TASK-20260816-024 - VDC lock readiness minimum instance gate

- 状态：完成代码、文档、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 按最快形成 DPLL 锁定和 VDC 同步的方向，重排 VDC P0 优先级。
  - 建立不绕过 HAOFV 的 VDC 最小实例观测入口：TDMA schedule、timestamp dictionary、SYNC_IO observer adapter、DPLL gate 和 VdcVector 只读快照。
  - 固化 readiness HIL，明确当前阻塞在 diagnostic timestamp，而不是误报 DPLL lock。
- 完成内容：
  - `VDC_DOMAIN_TODO.md` 增加 DPLL 锁定 / VDC 同步最快闭环优先级，定义 P0.0-P0.4：readiness、PIO edge latch、eligible sample admission、DPLL 环路滤波器、DCO snapshot。
  - 新增 `SYSTem:SYNC:VDC:LOCK:READiness?`，返回 `input_ready` 与 `locked` 两个独立布尔值，并输出 reason、DPLL/quality 状态、observer 计数、timestamp eligibility、dictionary/profile CRC 和最近 payload/source/reference。
  - 新增 `tools/vdc_lock_readiness_validate/vdc_lock_readiness_validate.py`，用 COM5/COM6 验证 observer 启用、采样启动、forced edge、VDC gate 拒绝和 readiness reason。
  - 同步 `SCPI_COMMANDS.md`、`SCPI_COMMAND_PLAN.md`、中文 SCPI 指令表、`VDC_DOMAIN_ARCHITECTURE.md` 和 `tools/README.md`。
- 验证结果：
  - `python -m py_compile tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\product_scpi_validate\product_scpi_validate.py` 通过。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=129，包含 `SYSTem:SYNC:VDC:LOCK:READiness?`。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，generated=66。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816044803`，OTA package CRC `0x5DB51D7B`。
  - COM5 OTA 到 build `20260816044803`，`ota_boot_commit.py` 被启动日志串扰误判失败；独立查询确认 `SYST:FW:BUILD? -> "20260816044803"`、`SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`、`SYST:ERR? -> 0,"No error"`。
  - COM6 OTA/commit 到 build `20260816044803`，`ota_boot_commit.py` 通过。
  - `python tools\vdc_lock_readiness_validate\vdc_lock_readiness_validate.py COM5 COM6 --expected-build 20260816044803 --out-dir build-rtos-multicore-smoke\vdc_lock_readiness_validate_20260816044803` 通过：COM5/COM6 均为 `reason=5,submitted=1,accepted=0,rejected=1,gate=9,source=2,resolution_ns=4,flags=1`。
- 还需完成：
  - P0.1 接真实 PIO edge latch，使 observation sample 不再带 `DIAGNOSTIC_ONLY`。
  - P0.2 验证 `DPLL_ELIGIBLE` 样本 accepted_count 增长且 `last_gate_reject_code=0`。
  - P0.3 实现 `SyncDpllFB` 最小环路滤波器，由 phase/frequency error 调节 VDC clock model 的 offset/rate，不能继续依赖 sample count 伪锁定。
- 关联文件：
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tools/vdc_lock_readiness_validate/vdc_lock_readiness_validate.py`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
- 下一步：
  - 优先把 TDMA/PIO 基础件的真实 edge timestamp latch 接到 VDC observation window，形成可被 DPLL gate 接受的最小硬件样本。

### VDC-TASK-20260816-023 - Default observer dictionary and open wrap anchor

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 修复默认 VDC timestamp dictionary 为空导致 observer 样本先被 `BAD_FRAME` 拒绝的问题。
  - 修复 VDC 启动后第一帧 `tick_l32` 可能大于半量程而被 wrap tracker 当作 stale 的问题。
  - 在板端验证 diagnostic hardware tick 样本会进入 VDC admission，并被 `TIMESTAMP_NOT_ELIGIBLE` 正确拒绝。
- 完成内容：
  - `vdc_domain_init()` 现在发布最小 bring-up dictionary：event 1/2 均绑定为本机 `HARDWARE_TICK` observation sample，payload 为 `SYNC_SAMPLE`。
  - `VdcWrapTracker` 增加 `anchor_valid` 和 `vdc_wrap_tracker_init_open()`；默认 VDC context 使用 open-anchor，让第一帧真实 tick 建立本地锚点。
  - `vdc_latch_validate.py` 通过 `initial_sample_mask=1` 强制制造一个 falling event，断言 `submitted` 增长、`accepted` 不增长、`rejected` 增长、`last_gate_reject_code=9`。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816042527`，OTA package CRC `0xDF65F48F`。
  - COM5/COM6 均 OTA 到 build `20260816042527` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `python tools\vdc_latch_validate\vdc_latch_validate.py COM5 COM6 --expected-build 20260816042527 --out-dir build-rtos-multicore-smoke\vdc_latch_validate_20260816042527_gate` 通过：COM5 `submitted=1,accepted=0,rejected=1,gate=9,source=2,resolution_ns=4,flags=1`；COM6 `submitted=1,accepted=0,rejected=1,gate=9,source=2,resolution_ns=4,flags=1`。
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816042527 --out-dir build-rtos-multicore-smoke\vdc_observer_validate_20260816042527` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=4263841859`。
- 下一步：
  - 继续实现 PIO edge latch 或 DMA/IRQ timestamp 入口；只有去掉 `DIAGNOSTIC_ONLY` 且置 `DPLL_ELIGIBLE` 后，`accepted_count` 才应该增长。

### VDC-TASK-20260816-022 - Timer1 hardware tick latch admission guard

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 把 SYNC_IO core1 latch 的时间戳从软件微秒时间基升级到 `timer1/CLK_SYS` hardware tick。
  - 修正 VDC compact observation contract，确保采样事实的 timestamp source/resolution/flags 只能来自 latch fact，不能由 dictionary 默认值抬高。
- 完成内容：
  - `VdcCompactObservationSample` 增加 `timestamp_source`、`timestamp_resolution_ns`、`timestamp_flags`。
  - `VdcSyncIoAdapter` 要求 decode config 显式提供 timestamp metadata，并复制到 compact sample。
  - `vdc_dpll_manager_sync_io_observer_service()` 把 `sync_io_read_capture_latched()` 输出的 timestamp metadata 传入 adapter。
  - `vdc_domain_expand_compact_observation()` 拒绝 source 与 dictionary 不一致的 compact sample，并使用 compact sample 的 timestamp metadata 生成 evidence。
  - 单元测试覆盖 diagnostic source elevation、diagnostic hardware flag rejection 和 adapter timestamp 透传。
- 边界：
  - 当前可报告 `HARDWARE_TICK / <=100 ns / DIAGNOSTIC_ONLY`，用于 TDMA/VDC bring-up 和 HIL 观测。
  - 因为 timestamp 仍是在 core1 drain FIFO 时刻读取，尚未实现 PIO edge latch；VDC gate 必须继续拒绝其作为正式 DPLL lock evidence。
- 验证结果：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 通过，generated=66。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816040434`，OTA package CRC `0x84D58EA6`。
  - COM5/COM6 均 OTA 到 build `20260816040434` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `vdc_latch_validate.py` 通过：COM5 `latched=118->270, observer_raw=0->274, source=2, resolution_ns=4, flags=1`，COM6 `latched=118->270, observer_raw=0->276, source=2, resolution_ns=4, flags=1`。
  - `vdc_observer_validate.py` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 下一步：
  - 继续实现 PIO/DMA/IRQ/core1 edge latch，使 `DPLL_ELIGIBLE` 样本进入 observation window。

### VDC-TASK-20260816-021 - Sync IO core1 latch observer source

- 状态：完成代码、host/build 和 COM5/COM6 HIL。
- 日期：2026-08-16
- 任务目标：
  - 将 VDC observer 的输入源从直接读取 PIO raw FIFO，改为读取 SYNC_IO owner 发布的 core1 latched capture fact。
  - 保持 HAOFV 边界：SYNC_IO/core1 只产本地 IO timestamp fact；VDC manager 只做 adapter 和提交；VDC domain 继续执行 dictionary/wrap/gate/DPLL admission。
- 完成内容：
  - `app_realtime_run_once()` 调用 `sync_io_capture_latch_service_core1()`，让 core1 realtime loop 负责搬运 capture FIFO。
  - `sync_io_capture_latched_word_t` 携带 raw word、sample seq、base time、sample period、timestamp source/resolution/flags 和 drop evidence。
  - `vdc_dpll_manager_sync_io_observer_service()` 改为读取 `sync_io_read_capture_latched()`；last timestamp evidence 使用 latch fact 的 source/resolution/flags，dictionary 只补节点和 payload 语义。
  - 新增 `REALtime:IO:SAMPle:LATCh?` 只读查询，给 HIL 读取 latch source/resolution 和 drop 证据。
- 验证结果：
  - `python -m py_compile tools\vdc_latch_validate\vdc_latch_validate.py tools\vdc_observer_validate\vdc_observer_validate.py tools\realtime_scpi_validate\realtime_scpi_validate.py` 通过。
  - `python tools\realtime_scpi_validate\realtime_scpi_validate.py --dry-run` 生成 66 条，包含 `REALtime:IO:SAMPle:LATCh?`。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=128。
  - `cmake --build build-rtos-multicore-smoke` 通过，build id `20260816034347`，OTA package CRC `0x962F5B65`。
  - COM5/COM6 均 OTA 到 build `20260816034347` 并 commit；两板 `SYSTem:ERRor?` 均为 `0,"No error"`。
  - `vdc_latch_validate.py` 通过：COM5 `latched=117->270, observer_raw=31->307`，COM6 `latched=120->272, observer_raw=31->310`，两板 timestamp source/resolution 均为 `1 / 1000 ns`。
  - `vdc_observer_validate.py` 通过，两板 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
- 还需完成：
  - 把 phase 1 的 `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY` latch 升级为 hardware tick diagnostic latch，再继续推进 PIO/DMA/IRQ/core1 edge latch。
  - 在 COM5/COM6 上补启用态 HIL，记录 latch counter、observer submitted/accepted/rejected、timestamp source/resolution 和 gate result。
- 关联文件：
  - `components/sync_io/inc/sync_io.h`
  - `components/sync_io/src/sync_io.c`
  - `application/src/app.c`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_realtime_io_commands.h`
  - `middleware/scpi_port/src/scpi_realtime_io_commands.c`

### VDC-TASK-20260816-017 - Manager-side Sync IO observer pump

- 状态：完成 host/build 验证和 COM5/COM6 默认 observer 查询；默认关闭，真实 PIO/DMA timestamp latch 和启用态 HIL 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 将 `sync_io_read_capture_words()` 到 VDC compact observation 的任务接线落到 manager 层。
  - 保持 HAOFV 边界：SYNC_IO 只产 raw IO fact，VDC manager 负责适配和提交，VDC domain 负责 dictionary/wrap/gate/DPLL admission。
- 完成内容：
  - `vdc_dpll_manager` 增加 `vdc_dpll_manager_sync_io_observer_config_t` 和 status snapshot。
  - observer 默认关闭；启用时读取 bounded raw word batch，调用 `vdc_sync_io_capture_word_to_compact_observation()`，再提交 `vdc_domain_submit_compact_observation()`。
  - status 记录 raw/no-edge/ambiguous/bad-argument/submitted/accepted/rejected 计数，以及 last raw word、event id、tick_l32 和 gate reject code。
  - observer 不自动启动 SYNC_IO capture，不使用 `board_uptime_ms()` 构造 DPLL timestamp；timestamp base、sample period、event id、frame CRC 必须由上游显式配置。
  - 新增 `SYSTem:SYNC:VDC:OBServer?` 只读维护查询，用于 HIL 读取 observer status；查询不启动 capture、不投递样本、不改变 DPLL。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 通过，generated=127。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有既有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，最新生成 build id `20260816024745`，package CRC `0x028BC853`。
  - COM5/COM6 均通过 OTA 更新并 commit 到 build `20260816024745`；两板 `SYST:OTA:STAT?` 均返回 `"COMMITTED",1,"NONE",5`。
  - COM5 查询 `SYST:SYNC:VDC:OBServer?` 返回 18 个零字段，符合默认 disabled observer；`SYST:SYNC:VDC:STAT?` 返回 `1,1,170836,3552,106131,170836`，错误队列为 `0,"No error"`。
  - COM6 查询 `SYST:SYNC:VDC:OBServer?` 返回 18 个零字段，符合默认 disabled observer；`SYST:SYNC:VDC:STAT?` 返回 `1,1,50875,3554,34170,50875`，错误队列为 `0,"No error"`。
- 还需完成：
  - 增加启用态 HIL 报告，输出 observer status、dictionary CRC、profile CRC、timestamp source/resolution 和 gate evidence。
  - 将真实 PIO/DMA/core1 timestamp latch 接入 observer 配置来源，避免人工配置 base tick。

### VDC-TASK-20260816-018 - VDC observer maintenance configuration

- 状态：完成代码、文档、build 和两板启停态验证。
- 本轮完成：
  - 新增 `SYSTem:SYNC:VDC:OBServer` 维护配置命令；无参数或 `enabled=0` 关闭 observer 并重置 status。
  - 启用态参数进入 `vdc_dpll_manager_configure_sync_io_observer()`，复用 manager 合法性检查：batch 上限、event id、observed mask、sample period、frame CRC 和初始 mask。
  - 保持 HAOFV 边界：SCPI 只写维护配置，不启动 SYNC_IO capture，不提交伪造 timestamp，不越过 VDC dictionary/gate。
- 验证：
  - `python tools\product_scpi_validate\product_scpi_validate.py --dry-run` 生成 128 条，包含 `SYSTem:SYNC:VDC:OBServer -> 1`。
  - `tools\tests\run_vdc_domain_tests.ps1` 通过；`tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 17/17 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，build `20260816030427`，OTA package CRC `0x5D46BBBD`。
  - COM5/COM6 均 OTA boot/commit 到 build `20260816030427`。
  - COM5/COM6 启用 `SYST:SYNC:VDC:OBServer 1,1,1,2,1,0,0,1000,0,0,1` 后查询得到 `enabled=1,max_words_per_service=1`；关闭后 observer 查询回到 disabled 全零字段，错误队列均为 `0,"No error"`。
- 下一步：
  - 在真实 timestamp latch/dictionary 配置到位后，再验证 raw/submitted/accepted/rejected 计数变化。

### VDC-TASK-20260816-019 - VDC observer evidence snapshot fields

- 状态：完成代码、文档、build 和 COM5/COM6 查询验证。
- 本轮完成：
  - `vdc_dpll_manager_sync_io_observer_status_t` 追加 observer 配置字段和 VDC 证据字段。
  - manager 在 capture word 解析出 compact observation 时记录 `last_edge_index`，并尝试通过 active `VdcTimestampDictionary` 展开 timestamp source/resolution/flags、source/reference slot 和 payload class。
  - `SYSTem:SYNC:VDC:OBServer?` 保持原 18 字段前缀不变，后续追加 22 个证据字段，合计 40 字段。
- 验证：
  - `product_scpi_validate.py --dry-run` 生成 128 条，`OBServer?` 解析为 40 字段。
  - VDC 单测通过，host 17/17 通过，docs check 通过但保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - build `20260816031400`，OTA package CRC `0x0F669557`。
  - COM5/COM6 均独立查询确认 committed：`SYST:FW:BUILD? -> "20260816031400"`、`SYST:OTA:STAT? -> "COMMITTED",1,"NONE",5`。
  - COM5/COM6 启用最小合法 observer 后查询到 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`、`dictionary_profile_crc32=974530568`；关闭后 40 字段全零。
- 注意：
  - `ota_boot_commit.py` 在本轮两块板上都被启动日志污染输出误判失败，独立 `scpi_query` 已确认 commit 实际成功；后续可优化该脚本的启动日志过滤。

### VDC-TASK-20260816-020 - VDC observer HIL validator script

- 状态：完成脚本固化和 COM5/COM6 验证。
- 完成：
  - 新增 `tools/vdc_observer_validate/vdc_observer_validate.py`，统一串口生命周期、关闭态检查、启用态配置、40 字段解析、schedule/dictionary CRC 校验、最终关闭和错误队列检查。
  - 更新 `tools/README.md`，把该脚本列入闭环验证工具。
- 验证：
  - `python tools\vdc_observer_validate\vdc_observer_validate.py COM5 COM6 --expected-build 20260816031400` 通过。
  - COM5/COM6 均返回 `schedule_crc32=974530568`、`dictionary_crc32=1814735745`。
  - COM5/COM6 验证 raw word -> compact observation -> VDC gate 的板端证据。
- 关联文件：
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `docs/interface/SCPI_COMMANDS.md`
  - `docs/interface/SCPI_COMMAND_PLAN.md`
  - `docs/sync/SYNC_IO_TODO.md`
  - `docs/sync/SYNC_IO_TASK_PROGRESS.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 优先补真实 timestamp latch 或维护查询；在该证据出现前，observer 不能被当作 100 ns DPLL lock 已闭环。

### VDC-TASK-20260816-016 - Sync IO capture adapter contract

- 状态：完成 host/build 验证；`sync_io_read_capture_words()` 到 adapter 的任务接线和板端 HIL 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 参考 RefMem 的适配层思路，在 VDC 侧建立 raw capture word 到 compact observation 的稳定 contract。
  - 保持 HAOFV 边界：adapter 只解析边沿和生成 compact fact，不访问 `sync_io` 内部状态，不写 DPLL offset/rate/lock。
- 完成内容：
  - 新增 `vdc_sync_io_adapter.h/.c`。
  - `vdc_sync_io_capture_word_to_compact_observation()` 支持 8 个 4-bit sample word、observed mask、rising/falling event id、sample period、base time 和 expected window。
  - adapter 输出 `VdcCompactObservationSample`；当前已纠偏为 latch fact 提供实际 source/resolution/flags，VDC active dictionary 只校验语义并补 source/reference slot 与 payload class，adapter 本身不声明样本可进入 DPLL。
  - 单元测试覆盖 rising edge 解码、no edge、ambiguous edge，以及 adapter 输出通过 `vdc_domain_submit_compact_observation()` 进入 VDC gate。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 在 `task_vdc_sync` 或 core1 realtime 边界读取 `sync_io_read_capture_words()` / capture ring，将真实 word 投递到 adapter。
  - 对 COM5/COM6 增加 HIL 证据：raw word、edge index、event id、tick_l32、dictionary CRC、gate result。
- 关联文件：
  - `components/vdc_domain/inc/vdc_sync_io_adapter.h`
  - `components/vdc_domain/src/vdc_sync_io_adapter.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 接实际 capture source，但仍不得把默认 1 MHz capture 或软件时间戳冒充为 100 ns hardware evidence。

### VDC-TASK-20260816-015 - Compact observation evidence gate

- 状态：完成 host/build 验证；真实 PIO/DMA capture ring 尚未接入
- 日期：2026-08-16
- 任务目标：
  - 参考 RefMem 的表契约思路，把 capture fact 进入 VDC 的路径收敛为 active dictionary + wrap tracker + evidence gate，而不是让 realtime IO 直接构造 DPLL sample。
  - 保持 HAOFV 边界：VDC owner 展开 timestamp fact 并执行 gate；`sync_io`/core1 只提供 compact observation，不写 offset/rate/lock。
- 完成内容：
  - 新增 `VdcCompactObservationSample`，作为 PIO/DMA/core1 capture fact 的最小载荷：`sample_seq`、`event_id`、`tick_l32`、expected window、CRC 和质量字段。
  - `vdc_domain_context_t` 增加 active `VdcTimestampDictionary` 与 `VdcWrapTracker`。
  - 新增 `vdc_domain_publish_timestamp_dictionary()`，要求 dictionary CRC、版本和 profile CRC 与 active TDMA schedule 匹配，发布时重置 wrap tracker。
  - 新增 `vdc_domain_expand_compact_observation()`，按 dictionary 校验 event/source 语义，按 wrap tracker 扩展 tick，再用 compact sample 自带的 source/resolution/flags 生成 `VdcTDMATimestampEvidence` 并经过 observation window gate。
  - 新增 `vdc_domain_submit_compact_observation()` 和 `vdc_dpll_manager_submit_compact_observation()` wrapper，后续 `task_vdc_sync` 可通过 manager 投递 capture fact。
  - 单元测试覆盖 compact observation 展开、dictionary CRC 拒绝、stale tick 拒绝、context submit accepted/rejected 计数。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 将 `sync_io` / PIO capture ring 的 raw word 转换为 `VdcCompactObservationSample`。
  - 增加板端 COM5/COM6 HIL：真实 PIO observation sample 报告 source、resolution、late、phase error、CRC 和 gate result。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 进入 `sync_io` capture word 到 `VdcCompactObservationSample` 的最小接线，仍不得把软件时间戳标成 100 ns hardware evidence。

### VDC-TASK-20260816-014 - Timestamp dictionary and wrap tracker contract

- 状态：完成 host/build 验证；capture ring 的完整 `seq_delta` 扩展待后续实现
- 日期：2026-08-16
- 任务目标：
  - 冻结 compact timestamp 展开和 32 位 tick 回绕扩展的基础规则，避免后续 PIO latch 直接把裸 event/tick 写进 DPLL。
  - 保持 HAOFV 边界：dictionary 和 wrap tracker 只生成 timestamp sample 的事实字段，不承载业务数据，不写 VDC offset/rate/lock。
- 完成内容：
  - `vdc_timestamp.h/.c` 增加 `VdcTimestampDictionary`：`event_id`、source/reference slot、source、resolution、default flags、port/signal 和 payload class。
  - dictionary 增加版本号、entry_count、profile CRC、dictionary CRC、entry 有效性和 event id 唯一性校验。
  - 新增 `vdc_timestamp_dictionary_apply()`，用于历史 latch sample 的 dictionary 展开；当前 compact observation admission 已收敛为 dictionary 校验 source/resolution，实际 flags 由 latch fact 提供。
  - 新增 `VdcWrapTracker`，支持 `tick_l32` 正向扩展、正向回绕识别、stale/pre-wrap 拒绝和小幅倒退容差。
  - 单元测试覆盖 dictionary CRC 篡改、重复 event id、compact sample 展开、tick 回绕和 stale/backward 拒绝。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 将 PIO/DMA capture ring 的 compact event/tick 接入 dictionary + wrap tracker。
  - 增加 System Pack / SD / SCPI profile loader 对 timestamp dictionary 的导入、激活和回滚。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp.h`
  - `components/vdc_domain/src/vdc_timestamp.c`
  - `tests/unit/test_vdc_domain.c`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 继续 `VDC_OBSERVATION_WINDOW` 的 hardware latch bring-up，先把 capture ring sample 接到该基础件，再进入 DPLL admission。

### VDC-TASK-20260816-013 - Timestamp contract split

- 状态：完成 host/build 验证；真实 PIO/DMA timestamp latch 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 将 DPLL timestamp admission 的基础规则从 `vdc_domain.c` 单体中拆出，形成后续硬件 latch、timestamp dictionary 和 wrap tracker 的接入口。
  - 保持 HAOFV 边界：timestamp 基础件只判断 source/resolution/flag/window，`SyncDpllFB` / VDC owner 仍是 offset/rate/lock 唯一 writer。
- 完成内容：
  - 新增 `vdc_timestamp.h/.c`，定义 `SOFTWARE_US`、`HARDWARE_TICK`、`DIAGNOSTIC_ONLY`、`DPLL_ELIGIBLE`、latch sample 和 admission code。
  - `vdc_domain_validate_tdma_timestamp_evidence()` 改为调用 `vdc_timestamp_dpll_admission_check()`；诊断时间戳、非硬件 tick 和超出 `100 ns` 分辨率的样本仍被拒绝进入 DPLL。
  - `vdc_timestamp_observed_in_window()` 统一 expected/observed window + guard 判断，为后续 PIO capture ring 映射提供公共规则。
  - 单元测试增加 timestamp helper 覆盖：软件诊断样本拒绝、硬件 tick 诊断标志拒绝、分辨率超限拒绝、`<=100 ns` 硬件样本通过。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过。
- 还需完成：
  - 接入真实 PIO/DMA/IRQ/core1 timestamp latch，产出 `HARDWARE_TICK / <=100 ns / DPLL_ELIGIBLE` 的 observation sample。
  - 冻结 `VdcTimestampDictionary` 和 `VdcWrapTracker`，把 compact timestamp 展开为节点、端口、信号语义和 64 位本地 tick。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp.h`
  - `components/vdc_domain/src/vdc_timestamp.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 继续 P3/P4，按 HAOFV 边界把硬件 capture ring 和 `VDC_OBSERVATION_WINDOW` sample bring-up 接到该 contract。

### VDC-TASK-20260816-012 - DPLL sample commits VDC clock and DCO snapshot

- 状态：完成 host/build 验证；真实 servo 和硬件 timestamp latch 待后续实现
- 日期：2026-08-16
- 任务目标：
  - 让 DPLL accepted sample 形成实质性的 VDC clock model 输出，而不是只推进 lock state 计数。
  - 保持 HAOFV 边界：`SyncDpllFB` / VDC owner 是 `offset/rate/lock` 唯一 writer，RefMem 和 core1/PIO 不写 clock model。
- 完成内容：
  - `vdc_dpll_state_t` 增加 `last_frequency_error_ppb`、`last_expected_window_start_ns` 和 `last_observed_time_ns`。
  - accepted hardware sample 后，VDC owner 根据 `phase_error_ns` 更新 `VdcClockModel.phase_offset_ns`，并同步派生 `VdcDcoControl`。
  - 连续样本之间通过 expected/observed period delta 估算 `frequency_error_ppb`，按 servo sanity limit 限幅，并写入 `clock.period_adjust_ppb` 和 `VdcErrorBudget.freq_offset_ppb/freq_skew_ppb`。
  - 单元测试覆盖 phase offset 写入 clock/DCO，以及 10 ms 样本间隔下的 100 ppb frequency error 估算。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815192045`，package CRC `0xFC02E459`。
- 还需完成：
  - 将该首版 period/phase 更新替换或升级为 `SyncDpllFB` 内部 PI/linreg servo。
  - 增加 outlier gate、servo reset、step/slew policy 和 HOLDOVER/RELOCK 状态迁移。
  - 接入硬件 timestamp latch 后，禁止诊断 timestamp 进入该更新路径。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 提交推送后进入硬件 timestamp latch 或 `SyncDpllFB` 组件化拆分。

### VDC-TASK-20260816-011 - RefMem TDMA consumes VDC data window plan

- 状态：完成 host/build 验证；COM5/COM6 HIL 待后续烧录轮次执行
- 日期：2026-08-16
- 任务目标：
  - 让 RefMem data frame 成为 VDC/TDMA schedule 上的 payload，而不是收到 intent 后立即执行。
  - 保持 HAOFV 边界：RefMem 不计算 TDMA 相位，只向 VDC manager 请求 `REFMEM_DATA_WINDOW` 计划；core1 realtime 只消费计划并执行窗口。
- 完成内容：
  - `refmem_realtime_tdma_intent_config_t` 增加 VDC window plan 字段：plan valid、window class、schedule CRC、window start/end、guard start/end。
  - `refmem_realtime_tdma_snapshot_t` 增加窗口计划和执行证据：window miss count、wait_ns、late_ns、window/guard 起止。
  - `refmem_realtime_tdma_core1_service()` 在未到 guard 前保持 pending 并返回 `WAITING_FOR_WINDOW`；错过 payload window 或 guard end 时返回 `WINDOW_MISSED`；进入 payload window 后才调用 TX/RX ops。
  - `DistributedRefMemAO` 的 NodeLoad AUTO TX/RX 提交前调用 `vdc_dpll_manager_plan_tdma_window(VDC_DOMAIN_WINDOW_REFMEM_DATA, ...)`，把 VDC-owned data window plan 下发给 core1 service。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 追加 VDC window plan 和 wait/late/miss evidence 字段，保留旧字段顺序。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815191550`，package CRC `0x25D841EE`。
- 还需完成：
  - COM5/COM6 烧录后验证 NodeLoad AUTO 在真实 PIO 25 MHz 环路中产生 `vdc_window_plan_valid=1`、wait/late/miss evidence。
  - 将同一机制扩展到 `VDC_OBSERVATION_WINDOW` 的 SYNC sample，并接硬件 timestamp latch。
  - 同步完整 SCPI 指令表中的 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 追加字段。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
- 下一步：
  - 提交推送后进入硬件 timestamp latch / observation window sample bring-up。

### VDC-TASK-20260816-010 - VDC quality table and error budget snapshot

- 状态：完成 host/build 验证；板端查询待后续 HIL 轮次执行
- 日期：2026-08-16
- 任务目标：
  - 按 HAOFV 边界补齐 VDC 自有的同步时钟健康评估机制。
  - 明确 TDMA sample 的 accepted/rejected、timestamp resolution、freshness、offset、jitter 和 gate reject 由 VDC owner 形成质量事实。
  - 避免 RefMem payload 同步成功被误当作 VDC/DPLL 健康结论。
- 完成内容：
  - `vdc_domain_snapshot_t` / `vdc_domain_context_t` 增加 `VdcQualityTable` 和 `VdcErrorBudget`。
  - `vdc_domain_submit_tdma_evidence()` 在 DPLL sample 通过或拒绝时同步更新 health state、sample counter、consecutive good/bad、timestamp source/resolution/flags、offset RMS/max、jitter、path delay、dispersion 和 root distance。
  - `vdc_domain_service()` 增加 sample age 刷新，形成 `last_sample_age_1e3ns`，为后续 HOLDOVER / stale / RUN gate 提供基础字段。
  - `vdc_dpll_manager_get_snapshot()` 提供只读 VDC snapshot 入口，保持 SCPI 读取 snapshot，不直接访问 VDC context。
  - `READ:SYNC:QUALity?` 从固定回复改为读取 VDC quality/error budget snapshot。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815190808`，package CRC `0xF1ACEE53`。
- 还需完成：
  - 将 `READ:SYNC:QUALity?` 的更新字段同步到 SCPI 指令表。
  - 接真实 DPLL servo 后补 frequency error、outlier gate 和 rate/skew 统计。
  - 将 VDC quality/error budget 接入 RUN gate 和 RefMem VdcSlot。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 提交推送后进入 TDMA plan 被 core1/PIO 消费的 P0 项。

### VDC-TASK-20260816-009 - VDC TDMA window planner contract

- 状态：完成并已通过 COM5/COM6 板端查询和两板 HIL
- 日期：2026-08-16
- 任务目标：
  - 为 core1/PIO 后续按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW` / `VDC_OBSERVATION_WINDOW` 提供 VDC-owned 计划契约。
  - 避免 RefMem 或 SCPI 自行计算窗口相位，保持 VDC 拥有 schedule/window/gate 语义。
- 完成内容：
  - 新增 `vdc_tdma_window_plan_t`，字段覆盖 `now_ns`、窗口起止、guard 起止、`wait_ns`、`late_ns`、inside/missed 标志、slot/reference 和 schedule CRC。
  - 新增 `vdc_domain_plan_tdma_window()`，从 active profile 计算当前或下一 TDMA window；窗口已错过 guard 后自动转入下一周期并记录 `missed_current_window`。
  - `vdc_dpll_manager` 增加只读 wrapper，SCPI 继续通过 manager 读取 VDC 计划，不直接访问内部 context。
  - 新增维护命令 `SYSTem:SYNC:VDC:TDMA:PLAN? [window_class],[now_ns_lo],[now_ns_hi]`，默认查询 `REFMEM_DATA_WINDOW` 当前窗口计划。
  - 同步更新 `SCPI_COMMANDS.md`、`SCPI_COMMAND_PLAN.md` 和 VDC 架构/TODO。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1` 通过。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815185032`，package CRC `0xB6C4C634`。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - OTA 到 COM5/COM6 后查询 `SYST:FW:BUILD?` 均为 `"20260815185032"`，`SYST:OTA:STAT?` 均为 `"COMMITTED"`。
  - COM5/COM6 执行 `SYSTem:SYNC:VDC:TDMA:PLAN? 2,15000,0` 均返回默认 `REFMEM_DATA_WINDOW`：`window_start_ns=20000`、`window_end_ns=820000`、`guard_start_ns=19000`、`guard_end_ns=821000`、`wait_ns=5000`、schedule CRC `974530568`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_vdc_plan` 通过；报告 build A/B 均为 `"20260815185032"`，`failures=[]`。
- 还需完成：
  - 让 `refmem_realtime_tdma` / core1 PIO service 消费该 plan，真正按 data/observation window 执行 TX/RX 和 timestamp latch。
- 关联文件：
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `components/vdc_dpll_manager/inc/vdc_dpll_manager.h`
  - `components/vdc_dpll_manager/src/vdc_dpll_manager.c`
  - `middleware/scpi_port/inc/scpi_sync_commands.h`
  - `middleware/scpi_port/src/scpi_sync_commands.c`
  - `tests/unit/test_vdc_domain.c`
- 下一步：
  - 增加 TDMA service 对 window plan 的消费字段和执行前等待策略。

### VDC-TASK-20260816-008 - COM5/COM6 VDC data-window diagnostic gate

- 状态：完成并已通过 COM5/COM6 HIL；正式 schedule wait / hardware latch 待实现
- 日期：2026-08-16
- 任务目标：
  - 复盘 COM5/COM6 `refmem_spi_hil_validate.py` 失败原因，区分真实物理链路问题和 VDC schedule/window 问题。
  - 保持 HAOFV 边界：RefMem 只提供 frame/payload/CRC/timestamp evidence，VDC gate 负责 schedule/window 判定，DPLL 不消费诊断 timestamp。
- 完成内容：
  - 确认 COM5/COM6 线序检测、25 MHz PIO TDMA RAW/HELLO/EPOCH/DELTA 均已通过，失败点为 `ACK_NACK` 的 VDC gate `BAD_FRAME`。
  - 失败原因收敛为当前 TDMA service 收到 intent 后立即执行，尚未按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW`；诊断 timestamp 落在默认 data window 外时会被 VDC gate 拒绝。
  - `refmem_vdc_bridge` 的 `frame_seq/sample_seq` 改为 TDMA `completed_seq`，避免把 RefMem 业务 `seq32/ack_seq32` 和物理 TDMA frame evidence 混用。
  - HIL 脚本保留对 RefMem RX、frame CRC、payload CRC、payload class 和 source slot 的检查；若 VDC gate 因窗口相位拒绝诊断样本，记录为“diagnostic timestamp outside active VDC data window”，不再误判为物理链路失败。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_vdc_bridge_tests.ps1` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815183936`，package CRC `0x690AF5D4`。
  - OTA 到 COM5/COM6 后查询 `SYST:FW:BUILD?` 均为 `"20260815183936"`，`SYST:OTA:STAT?` 均为 `"COMMITTED"`。
  - `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6 --out-dir build-rtos-multicore-smoke\refmem_spi_hil_vdc` 通过；报告确认 25 MHz PIO TDMA RAW/HELLO/EPOCH/DELTA/ACK/FENCE/QUALITY 双向链路成功，frame/payload CRC 有效。
  - HIL 报告中 `B_QUALITY_A` 的 VDC data evidence 为 `REJECTED` / `VDC_GATE_BAD_FRAME`，脚本记录为 `diagnostic timestamp outside active VDC data window`，用于证明当前还缺少按 active VDC data window 执行的 TDMA scheduler，不作为 100 ns DPLL evidence。
- 还需完成：
  - core1/PIO TDMA 必须按 active VDC schedule 等待 data/observation window，并增加硬件 timestamp latch；当前诊断 gate 通过或窗口拒绝都不得作为 100 ns DPLL lock evidence。
- 关联文件：
  - `components/distributed_refmem/src/refmem_vdc_bridge.c`
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 实现 core1/PIO 按 `VdcTdmaScheduleProfile` 等待 `REFMEM_DATA_WINDOW` / `VDC_OBSERVATION_WINDOW` 的执行路径，再接硬件 timestamp latch。

### VDC-TASK-20260816-007 - Two-board RefMem TDMA VDC evidence HIL script

- 状态：完成脚本编译检查；COM5/COM6 首次复跑暴露 VDC data window 调度缺口，已转入 VDC-TASK-20260816-008
- 日期：2026-08-16
- 任务目标：
  - 将 `SYSTem:REFMEM:SYNC:TDMA:VDC?` 纳入两板 RefMem PIO TDMA HIL 验收。
  - 对 `DELTA/ACK_NACK/FENCE/QUALITY` 交换记录 VDC bridge/gate/CRC/timestamp evidence，作为后续硬件 timestamp latch 的对照基线。
- 完成内容：
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py` 的 `ExchangeResult` 增加 `vdc_response/vdc_passed/vdc_reason`。
  - `tdma_exchange()` 在 RX frame 处理后查询接收板 `SYSTem:REFMEM:SYNC:TDMA:VDC? <receiver_local_slot>,<reference_slot>`。
  - 对支持的 RefMem data evidence frame 要求 VDC bridge `ACCEPTED`、gate pass、window class 为 `REFMEM_DATA`、timestamp 为 `SOFTWARE_US/1000 ns/DIAGNOSTIC_ONLY`。
  - `HELLO/EPOCH` 仍只记录为非 data evidence，不作为 VDC bridge 通过条件。
- 验证结果：
  - `python -m py_compile tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py` 通过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1 -HostGccDir D:\Embedded\GCC\mingw64\bin` 通过，17/17 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 文件命名 warning。
  - `git diff --check` 通过，仅有 CRLF 提示。
  - `cmake --build build-rtos-multicore-smoke` 通过，生成 build id `20260815182630`，package CRC `0x8ABF373F`。
- 还需完成：
  - 烧录包含 `SYSTem:REFMEM:SYNC:TDMA:VDC?` 的固件到 COM5/COM6。
  - 运行 `python tools\refmem_spi_hil_validate\refmem_spi_hil_validate.py --port-a COM5 --port-b COM6`，保存 report。
- 关联文件：
  - `tools/refmem_spi_hil_validate/refmem_spi_hil_validate.py`
- 下一步：
  - 跑全量 host/build 验证，通过后提交推送，再进入烧录和 COM5/COM6 HIL。

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
