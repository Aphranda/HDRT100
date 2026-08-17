# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-08-17

本文档维护 TDMA foundation 的独立待办。这里记录影响上/下行 TDMA、ring runtime、payload registry、adapter、completion、quality、HAOFV system node 和 HIL 验收的事项。

## P0 - 主域边界建立

- [x] 建立 `docs/tdma/README.md`。
- [x] 建立 `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`。
- [x] 建立 `docs/tdma/TDMA_DOMAIN_TODO.md`。
- [x] 建立 `docs/tdma/TDMA_TASK_PROGRESS.md`。
- [x] 更新 `docs/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 和 `docs/arch/HAOFV_ARCHITECTURE.md`，把 TDMA 加为 HAOFV 内部基础主域。
- [x] 清理 VDC 文档中的 “VDC owns TDMA ring” 语义，改为 “VDC consumes TDMA observation/evidence”。

## P1 - Runtime 契约

- [x] 在 `tdma_service_snapshot_t` 增加 ring runtime 字段：enabled、config seq、service seq、node count、local/reference slot、UP/DOWN group、running state、ring seq、last error、profile CRC、schedule CRC。
- [x] 增加 `tdma_service_configure_ring_runtime()`，由 TDMA owner 接收 active ring profile/runtime 配置。
- [x] 增加单元测试，验证公共 TDMA ring runtime 不伪造 closed-loop evidence。
- [x] RefMem 兼容层提供正式 foundation-profile 配置入口，并将 profile/ring 字段投影到只读 snapshot。
- [x] `SYSTem:REFMEM:SYNC:TDMA:STATus?` 保留旧字段顺序并追加 active profile、ring config/runtime 和 feedback evidence。
- [x] 将 ring runtime 从 `tdma_service.c` 单体拆成 `tdma_ring_runtime.*`，保留 `tdma_service` 聚合 API。
- [x] 将 payload registry 从 `tdma_service.c` 单体拆成 `tdma_payload_registry.*`，支持 System Pack / DeploymentGate 查询。
- [x] registry snapshot 暴露 config/registration seq、used/admitted/reject 水位和 last result，并追加到 TDMA 维护查询末尾。
- [x] 冻结 ring runtime reason code：direction conflict、adapter missing、timestamp missing、payload starvation、window missed、resource conflict；后续 adapter/scheduler 逐项接入发布源。

## P2 - HAOFV System Node / Resource Claim

- [x] 将 TDMA 表达为可装载 HAOFV system node / FB instance；首版增加唯一 `TdmaSchedulerAO` owner、TDMA baseline capability 和 DeploymentGate check。
- [x] 建立 `tdma_foundation_profile_t`，声明 ring、adapter、PIO/SM、DMA、core1 service、short/long capacity、payload whitelist、traffic class 和 IO/IP claim，并由 `tdma_service` 冻结到 runtime snapshot。
- [x] 将 foundation profile 纳入 RMTP/System Pack 第 10 张正式表镜像，并从 NodeLoad / SlotClaim / RealtimeCapabilityContract 派生 owner 与资源绑定。
- [x] DeploymentGate 拒绝第二个 TDMA owner。
- [x] DeploymentGate 拒绝业务模型复用 TDMA communication adapter IO。
- [x] DeploymentGate 拒绝缺失 VDC/RefMem foundation payload 或 traffic class 重叠的 profile。
- [x] 支持板卡能力通过 SD System Pack 和 SCPI staging 加载，不能在代码中写死模型实例。
- [x] 将 prepared `TdmaFoundationProfile` 与 VDC ring、schedule CRC 和 cycle period 交叉校验，激活成功后由 TDMA owner 自动配置 runtime。

## P2A - TSN-style 资源管理与流控

- [x] 冻结五类 traffic class：VDC realtime、RefMem realtime、config control、OTA bulk、LOG best effort。
- [x] 为每类流定义 payload mask、周期预留字节、每周期最大帧数、队列深度、deadline、gate/shaping/preemption 和 overflow policy。
- [x] payload registry 按 active foundation profile whitelist 做 admission，拒绝未登记 payload。
- [x] TDMA scheduler 建立逐类固定队列和 time-aware gate；冻结 `VDC > RefMem > maintenance`，maintenance gate 默认关闭，配置/OTA/LOG 不抢占实时短帧。
- [x] 将 VDC/RefMem 的产品路径收敛到唯一 `TdmaSchedulerAO` runtime owner，core1 每轮只推进一次公共 service；保留 domain wrapper，不保留第二套 runtime。
- [ ] 实现逐流 policing、backpressure、drop/retry/deadline/budget overrun 计数，并发布 `TdmaQualityVector`；基础计数和 per-class completion token 已完成，正式 RefMem vector 映射尚未完成。
- [x] DeploymentGate 首版校验总周期预算、guard band、short/long MTU、queue RAM、PIO/SM/DMA/IO/IP claim，不允许 profile overcommit；后续补板级 DMA channel/PIO block 全局仲裁表。
- [ ] OTA 支持续传和 producer pause；LOG 允许 drop-oldest，但二者都不得阻塞 core1 或侵占 guard band。
- [ ] 按 RefMem region/slot criticality 拆分 critical delta 与 background delta，避免全部 64 KB 事实同步都占用硬预留窗口。
- [ ] 多环/冗余阶段评估 FRER-style sequence 与 duplicate elimination；首版不宣称冗余能力。

## P3 - 上/下行同时运行

- [x] 建立 `TdmaRingAdapterOps` 契约，由 adapter 的 start/stop/service evidence 驱动 `up_running/down_running`；未绑定 adapter 时明确报告 `ADAPTER_MISSING`。
- [x] 冻结 reference TX / feedback RX 相关门禁：sequence、frame CRC、schedule CRC、时间戳顺序、feedback timeout、硬件 latch 标志和 `<=100 ns` 分辨率必须同时成立。
- [ ] core1 TDMA runtime 同时服务 `TDMA_UP_LEG` 和 `TDMA_DOWN_LEG`。
- [ ] 空闲无业务 payload 时持续发送/接收 `IDLE_BEACON` 或等价 freshness 帧。
- [x] runtime snapshot 暴露 `up_running/down_running/ring_seq/last_error`、adapter lifecycle、idle beacon 计数和反馈相关字段；running 来自 adapter，但不单独等同于硬件闭环 evidence。
- [ ] `simultaneous_feedback_loop_evidence` 只由硬件 RX/TX timestamp 相关性置位。
- [ ] host 监控工具默认只读 TDMA runtime，不通过串口查询参与续窗。

## P4 - Completion / Reliability

- [x] 将 result/error/timestamp/frame completion 按 traffic class 持久化，不能让后完成的 RefMem/maintenance 帧覆盖 VDC observation metadata。
- [ ] 为 RefMem AUTO NodeLoad 增加 ACK/重发/fence completion。
- [ ] TDMA 每条 delta 必须有 `origin_encoded -> queued -> sent -> received -> validated -> committed -> acked/fenced` evidence。
- [ ] `WINDOW_MISSED`、RX timeout、duplicate seq、CRC error 必须触发有界 retry/backoff 或明确 NACK。
- [ ] 增加 quality table 映射：timeout、late、drop、overrun、direction conflict、timestamp missing。

## P5 - VDC Observation Evidence

- [ ] 冻结 ring frame timestamp evidence：reference TX、每 hop RX/TX、feedback RX、schedule CRC、frame CRC、timestamp source/resolution/flags。
- [x] 冻结两板首版 reference TX / feedback RX 最小相关结构和只读 snapshot；多节点逐 hop evidence table 尚未完成。
- [ ] TDMA observation window 产生 `HARDWARE_TICK / <=100 ns / !DIAGNOSTIC_ONLY` 样本后，VDC 才允许 DPLL accepted。
- [ ] 软件时间戳、host 耗时、单向 leg self-test 只能作为 diagnostic evidence。
- [ ] 长监控末端输出 summary + SVG，区分 leg monitor、TDMA ring runtime 和 VDC lock quality。

## P6 - Adapter 迁移

- [ ] PIO SPI adapter 只作为最小系统 bring-up adapter，不能成为架构绑定。
- [ ] BISS-C adapter 作为后续类 IP 核，提供编码/解码、timestamp、CRC 和 quality。
- [ ] UART / RS485 adapter 明确 MTU、latency、timeout 和降级质量语义。
- [ ] 所有 adapter 复用同一 TDMA payload/window/completion contract。

## P7 - HIL 验收

- [ ] 两板最小系统同时 UP/DOWN 常驻 5 min。
- [ ] 验收 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=true`、`WINDOW_BOUND` 不作为最终态、`BAD_FRAME=0`。
- [ ] 复测 RefMem AUTO NodeLoad 双向同步，确认 ACK/重发/fence 不依赖偶然窗口命中。
- [ ] 复测 VDC observation，确认 DPLL accepted sample 可追溯到 TDMA ring evidence。
- [ ] 扩展 3 节点、5 节点 profile，验证只扩表不改算法。
