# SYNC_IO 任务进度

Status: Active
Domain: SYNC_IO
Canonical: `docs/sync/SYNC_IO_TASK_PROGRESS.md`
Related: `docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`, `docs/sync/SYNC_IO_REFACTOR_PLAN.md`, `docs/storage/LOG_SYSTEM_TODO.md`
Last updated: 2026-07-08

本文档记录 SYNC_IO / Trigger 同步重构相关任务的闭环验证、风险和后续动作。

### SYNC_IO-TASK-20260707-001 - TriggerFB RESET/FAULT 语义收敛

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
- 涉及文件：`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260707-002 - BiSS timeout/sample scan 闭环

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
- 涉及文件：`components/sync_trigger/inc/biss_node_io.h`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_trace_decode/sd_trace_decode.py`，`tools/biss_board_validate/biss_board_validate.py`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260707-003 - Resource owner 边界收口

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
- 涉及文件：`components/sync_trigger/inc/trigger_resource_map.h`，`components/sync_trigger/src/trigger_resource_map.c`，`components/sync_trigger/src/trigger_fb.c`，`components/sync_trigger/src/sync_trigger.c`，`tools/sd_board_validate/sd_board_validate.py`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260708-004 - BiSS TAP 物理 ARM 边界收口

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
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/inc/sync_io_mode_biss_tap.h`，`components/sync_io/src/sync_io_mode_biss_tap.c`，`components/sync_trigger/src/biss_node_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260708-005 - SEQ_STEP 物理实现搬迁

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
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_seq_step.c`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260708-006 - ENC_COUNT 物理实现搬迁与 P0-04 收口

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
- 涉及文件：`components/sync_io/src/sync_io_core_internal.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_enc_count.c`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`。

### SYNC_IO-TASK-20260708-007 - SYNC_IO P1 架构小项收口

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
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/inc/sync_io_mode.h`，`components/sync_io/src/sync_io.c`，`components/sync_io/src/sync_io_mode_*.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_resource_map.c`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`，`docs/sync/SYNC_IO_RESOURCE_PLAN.md`，`docs/interface/SCPI_COMMANDS.md`。

### SYNC_IO-TASK-20260708-008 - RJ45_TRIG 硬件定义优先收口

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
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/inc/sync_trigger.h`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_REFACTOR_PLAN.md`，`docs/sync/SYNC_IO_RESOURCE_PLAN.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/communication/BISSC_TAP_BRIDGE_DESIGN.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`。

### SYNC_IO-TASK-20260708-009 - ENC_COUNT 3-pin 软件定义收口

- 目标：按硬件定义优先原则固定 `GPIO19/RJ45_TRIG_IN`，将 ENC_COUNT 软件定义收口为 A/B/Z=`GPIO16/GPIO17/GPIO18`，避免 ENC 再占用 IN3。
- 完成：`SYNC_IO_HW_ENC_Z_PIN` 改为 `BOARD_SYNC_INPUT_BASE_PIN + 2`；`sync_io_hw_enc_pins_valid()`、TriggerVector 默认值和 `TRIG:ENC:APIN` 事件载荷均使用 A=16、B=17、Z=18。
- 完成：`enc_count.pio` 从 4-pin 采样改为 3-pin contiguous 采样，Z 从 bit2 提取；PIO init/disarm 只初始化和释放 GPIO16..18，不再触碰 GPIO19。
- 完成：`sync_io_enc_count_mode_validate()` 只接受 `in_pin_base=16` 且 Z=`base+2`；`TRIG:ENC:APIN 26` 继续作为关闭能力返回执行错误。
- 完成：同步更新 HAOFV、SYNC_IO、SCPI、BiSS 和 Trigger 文档，明确 `GPIO19` 是 `RJ45_TRIG_IN`，`GATE_IN` 只是模式层解释，ENC Z 不再使用 IN3。
- 验证：`cmake --build build-codex-sync-refactor` 通过，生成 build id `20260707172833` 的 factory/update 产物。
- 验证：`python -m py_compile tools\sd_board_validate\sd_board_validate.py tools\sd_trace_decode\sd_trace_decode.py tools\biss_board_validate\biss_board_validate.py` 通过。
- 验证：`git diff --check boards/rp2350_trig/inc/board_config.h components/sync_io/inc/sync_io_hw_profile.h components/sync_io/src/enc_count.pio components/sync_io/src/sync_io_mode_enc_count.c components/sync_trigger/inc/trigger_vector.h docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md docs/arch/HAOFV_ARCHITECTURE.md docs/interface/SCPI_COMMANDS.md docs/sync/SYNC_IO_RESOURCE_PLAN.md docs/sync/SYNC_IO_REFACTOR_PLAN.md docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md docs/communication/BISSC_TAP_BRIDGE_DESIGN.md docs/communication/BISSC_IMPLEMENTATION_TODO.md docs/trigger/TRIGGER_SYNC_TODO.md docs/trigger/TRIGGER_ENC_COUNT_DESIGN.md docs/trigger/TRIGGER_PULSE_COUNT_ANALYSIS.md` 通过，仅有既有 CRLF warning。
- 验证：`picotool reboot -f -u` 后 `picotool load -x build-codex-sync-refactor\RP2350_TRIG_FACTORY.uf2` 烧录并启动应用成功；板端 `SYST:FW:BUILD?` 返回 `"20260707172833"`。
- 验证：`python tools\sd_board_validate\sd_board_validate.py COM5 --validate-resource-owner --validate-trigger-release --out-dir build-codex-sync-refactor\sd_validation_enc_3pin_pinout_final` 通过，`summary.txt` 为 `PASS`。
- 验证：`python tools\biss_board_validate\biss_board_validate.py COM5 --out-dir build-codex-sync-refactor\biss_validation_enc_3pin_pinout_final` 通过，确认 BiSS TAP 未被 ENC pinout 调整回退。
- 验证：板端 `TRIG:ENC:APIN?` 返回 `16,17,18`；执行 `TRIG:ENC:APIN 26` 后 `SYST:ERR?` 返回 `-200,"Execution error"`，再次查询仍为 `16,17,18`。
- 风险：`docs/archive/TASK_PROGRESS.md` 中仍保留迁移前历史记录的旧 ENC 16/17/19 描述；按文档规则该文件作为全局历史保留，不作为当前硬件约束入口。
- 后续：如继续推进 P2 自检，应在板端闭环脚本中增加 ENC A/B/Z loopback 或外部回放验证，覆盖真实 A/B/Z 脉冲输入，而不仅是 SCPI 配置与资源 owner 断言。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/enc_count.pio`，`components/sync_io/src/sync_io_mode_enc_count.c`，`components/sync_trigger/inc/trigger_vector.h`，`components/sync_trigger/src/trigger_fb.c`，`docs/sync/SYNC_IO_REFACTOR_PLAN.md`，`docs/sync/SYNC_IO_RESOURCE_PLAN.md`，`docs/interface/SCPI_COMMANDS.md`，`docs/arch/HAOFV_ARCHITECTURE.md`，`docs/arch/HAOFV_IMPLEMENTATION_PLAYBOOK.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。

### SYNC_IO-TASK-20260708-010 - SYNC_CLK_OUT AUX2 运行路径迁移

- 目标：完成 P2-04 中 `SYNC_CLK_OUT` 从旧 GPIO22/`pio1/sm1` 到 AUX2/GPIO28/`pio2/sm2` 的运行路径迁移，保持硬件定义优先。
- 完成：`BOARD_SYNC_SYNC_CLK_OUT_PIN` 解析到 `BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`；新增 `BOARD_SYNC_MODE_OUT2_PIN` 表达 GPIO22 仍是主口 OUT2/模式本地输出。
- 完成：`sync_io_start_clock()` 改用 `BOARD_SYNC_PIO_AUX`、`BOARD_SYNC_AUX2_SM`、`BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN`，并在启动期间持有 `PIO2 + AUX` 资源；停止时释放资源并恢复 AUX2 输入安全态。
- 完成：TriggerFB 在 `OUTP:CLOC:*` 对应事件中检查 `sync_io_start_clock()` 结果，失败时同步真实 clock 状态并设置 `TRIG_ERROR_RESOURCE_CONFLICT` 或 `TRIG_ERROR_IO_ARM_FAILED`。
- 完成：`sync_io_hw_profile.h` 增加主口、RJ45_TRIG_IN/OUT、ARM_IN、EXT_CLK_IN、SYNC_CLK_OUT、AUX3 和 deprecated marker alias 的编译期断言。
- 验证：`cmake --build build-codex-rj45-interface` 通过，生成 factory/update 产物。
- 风险：`ARM_IN`、`EXT_CLK_IN` 仍是语义占位，旧低层宏只做 pull-down/诊断采样；后续需要迁移到 AUX0/AUX1 并接入 TriggerFB 资格/外部时钟逻辑。
- 涉及文件：`boards/rp2350_trig/inc/board_config.h`，`components/sync_io/inc/sync_io_hw_profile.h`，`components/sync_io/src/sync_io.c`，`components/sync_trigger/src/trigger_fb.c`，`docs/interface/SCPI_COMMANDS.md`，`docs/sync/SYNC_IO_RESOURCE_PLAN.md`，`docs/trigger/TRIGGER_SYNC_TODO.md`，`docs/sync/SYNC_IO_ARCH_REVIEW_TODO.md`，`docs/communication/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`，`docs/sync/SYNC_IO_TASK_PROGRESS.md`。
