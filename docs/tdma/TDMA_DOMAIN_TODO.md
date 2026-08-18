# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-08-18

本文档维护 TDMA foundation 的独立待办。这里记录影响上/下行 TDMA、ring runtime、payload registry、adapter、completion、quality、HAOFV system node 和 HIL 验收的事项。

## P0 - 主域边界建立

- [x] 建立 `docs/tdma/README.md`。
- [x] 建立 `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`。
- [x] 建立 `docs/tdma/TDMA_DOMAIN_TODO.md`。
- [x] 建立 `docs/tdma/TDMA_TASK_PROGRESS.md`。
- [x] 更新 `docs/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 和 `docs/arch/HAOFV_ARCHITECTURE.md`，把 TDMA 加为 HAOFV 内部基础主域。
- [x] 清理 VDC 文档中的 “VDC owns TDMA ring” 语义，改为 “VDC consumes TDMA observation/evidence”。

## P0.5 - 当前闭环阻塞项

目标：先补齐 TDMA ring runtime 的 adapter 与常驻环路证据，再进入 VDC/DPLL 闭环优化。当前 COM5/COM6 build `20260817104554` 已确认 RefMem 初始化正常，`SYSTem:REFMEM:STATus?` 末尾为 `1,8,0`；但 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 仍显示 `ring_up_running=0`、`ring_down_running=0`、`ring_last_error=4`、`simultaneous_feedback_loop_evidence=0`，其中 `4=ADAPTER_MISSING`。

- [x] P0.5-1：为当前最小系统 PIO SPI bring-up adapter 绑定 `TdmaRingAdapterOps`，让 `TdmaSchedulerAO` 在 core1 service 中能启动/停止/service ring adapter；该步骤只消除 `ADAPTER_MISSING`，不得伪造 `simultaneous_feedback_loop_evidence`。
  - 完成：新增 `tdma_pio_spi_ring_adapter.*`（transport 级，只处理 `TdmaTransportFrame`），由 `tdma_runtime_owner_init()` 绑定到唯一 `TdmaSchedulerAO`；`ADAPTER_MISSING` 消除，无物理路径时诚实报告 `EVIDENCE_MISSING`。
- [x] P0.5-2：ring adapter 首版发布生命周期 evidence：`adapter_started/start_count/stop_count/service_count/last_error`、`up_running/down_running`、idle beacon TX/RX 计数和 timestamp source/resolution/flags。
  - 完成：adapter 经 `tdma_ring_adapter_status_t` 发布上述字段并投影到 runtime snapshot；idle beacon 计数与 running 由物理 TX/RX 钩子驱动，未接物理前计数保持 0。
- [x] P0.5-3：实现两板同时 UP/DOWN 常驻短帧：空闲时持续发送/接收 `IDLE_BEACON` 或等价 process image short frame，不依赖 host 交替下发 `TX/RX` 维护命令维持窗口。
  - 进行中：`tdma_pio_spi_phys` 常驻物理层已改为**半双工单环**（每板下行 TX master + 上行 RX slave 两个独立 SM；实测对称接线：发送端闲置 RX/CS=`21`、TX/DATA=`23`、CLK=`24`，对端闲置 TX/CS=`16`、RX/DATA=`18`、CLK=`19`）。ring adapter 有 REFERENCE/FORWARD role，`set_phys_ctrl`/`set_phys` 连接物理层。
  - RX 可靠性（2026-08-18）：rx_byte SM 重写为 pico-examples 标准 **autopush 模式**（in_shift autopush threshold=8），根治手动 X 计数器导致的字节边界漂移（坏帧从 ~45% 降到 ~0）；DMA 双缓冲捕获 + magic 帧头扫描对齐（EtherCAT 式帧头锁定）；`SYSTem:SYNC:VDC:TDMA:PHYS?` 暴露 rx_bad/busy/magic_fail/magic 对齐分布诊断。
  - 发送（2026-08-18）：TSN 式确定性发送（每 TDMA cycle 固定相位发 beacon，替代 now-last 节流——后者因 sleep_until 提前唤醒跳过 ~40% tick）；当前**每 2 cycle 发一帧（~500 Hz）**：两板 core1 tick 自由运行导致 1 kHz 时接收查询与帧速率相等、性能随相位在 23%~92% 波动；500 Hz 时查询有 2 倍余量，环稳定 ~74% 好帧且 rx_bad≈0。**1 kHz 满速需要共享时基（P0.5-5 硬件 timestamp/DPLL）**。
  - OTA 安全（2026-08-18）：core1 在 OTA 会话期间跳过 TDMA service（`ota_ao_is_active`），flash lockout poll 保持紧凑，两板 OTA 稳定 PASS。两板烧录 HIL 常驻验证已跑（`tdma_ring_monitor/ring_rate_measure.py`）。
  - HIL 收敛（2026-08-18 build `20260818063242`）：COM5 `91274BA197662714` 作为 reference slot0，COM6 `73E940D75B406BCD` 通过 `SYSTem:TDMA:RING:LOCAL 1` 切为 forward slot1；10 s 只读 monitor 显示两板 `up_running=1/down_running=1`、COM6 forward 收发约 `492.5 frame/s`，COM5 reference 发约 `493.0 frame/s`、收回约 `416.3 frame/s`，resident ring 已不依赖 host 续窗。
- [x] P0.5-3A：验证 CS/frame-sync 三线单向腿是否消除 1 MHz 方向性丢帧。
  - 完成：下行链路发送端未用 RX/MISO 与接收端未用 TX/MISO 互连线改作 frame-sync/CS；按当前最小系统接线，`GPIO21->16` 为 CS，`GPIO23->18` 为 DATA，`GPIO24->19` 为 CLK。RX PIO 不再无条件连续采样，而是等待 CS 有效后按 SCK 采样；magic 扫描保留为保险和诊断。
  - 实测（2026-08-18 build `20260818072932`）：两板 OTA 后，`COM5->COM6` 与 `COM6->COM5` 都稳定在约 `473.6~473.7 frame/s`，`rx_bad=0`，方向性丢帧明显收敛。
  - 结论：当前最小系统下，CS/frame-sync 三线单向腿比无 CS 连续流更适合 1 MHz bring-up。
- [ ] P0.5-4：冻结并验证最小 feedback correlation：reference TX sequence、feedback RX sequence、identity CRC、schedule CRC、reference TX timestamp、feedback RX timestamp、round trip 和 timeout 必须来自同一圈 ring。
  - 进行中：`TdmaRingRuntime` correlation 逻辑已在，host 回环单测验证 sequence/identity CRC/schedule CRC/round trip 路径成立；两板 PIO SPI HIL 已证明 reference/forward 帧计数持续增长，但 PIO SPI `phys_tx` 仍返回 `0` timestamp，物理 round-trip correlation 仍未闭环。
- [ ] P0.5-5：只有当 timestamp 为 `HARDWARE_TICK`、分辨率 `<=100 ns`、带硬件 latch 标志且非 diagnostic-only 时，才允许 `simultaneous_feedback_loop_evidence=1`，并允许 VDC/DPLL 接受该样本。
  - 进行中：runtime 门禁（`<=100 ns`、`HARDWARE_LATCHED`、非 `DIAGNOSTIC_ONLY`）已有 host 单测覆盖（无硬件 timestamp / diagnostic-only 均拒绝）；两板 HIL 当前最终状态为 `TIMESTAMP_MISSING`、`simultaneous_feedback_loop_evidence=0`，下一步必须在 PIO/DMA 边界补 reference TX / feedback RX 硬件 latch。
- [ ] P0.5-6：扩展 HIL 脚本为只读监控 TDMA runtime，不通过串口查询参与续窗；5 min 验收必须记录 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=1`、`BAD_FRAME=0`、`WINDOW_BOUND` 不作为最终态，并在 `docs/temp/vdc_long_monitor/` 输出 summary + SVG。
- [ ] P0.5-7：P0.5 闭环通过后，再进入 DPLL 参数、水位和 reject 策略优化；在此之前 DPLL 曲线只能作为 leg/self-test 诊断参考，不能作为产品闭环质量结论。

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
- [x] 冻结长短帧门禁：VDC/RefMem realtime 只能使用 `SHORT`；OTA/SD reliable bulk 和 LOG 只能使用 `LONG`；配置流可按容量选择，但长帧必须经过 maintenance gate。
- [x] 增加 `STORAGE_BULK` payload class，并与 OTA 一起归入可靠 bulk traffic class，不为 SD 建立第二套 transport。
- [ ] 实现逐流 policing、backpressure、drop/retry/deadline/budget overrun 计数，并发布 `TdmaQualityVector`；基础计数和 per-class completion token 已完成，正式 RefMem vector 映射尚未完成。
- [x] DeploymentGate 首版校验总周期预算、guard band、short/long MTU、queue RAM、PIO/SM/DMA/IO/IP claim，不允许 profile overcommit；后续补板级 DMA channel/PIO block 全局仲裁表。
- [ ] OTA 支持续传和 producer pause；LOG 允许 drop-oldest，但二者都不得阻塞 core1 或侵占 guard band。
- [ ] 按 RefMem region/slot criticality 拆分 critical delta 与 background delta，避免全部 64 KB 事实同步都占用硬预留窗口。
- [ ] 多环/冗余阶段评估 FRER-style sequence 与 duplicate elimination；首版不宣称冗余能力。

## P3 - 上/下行同时运行

- [x] 建立 `TdmaRingAdapterOps` 契约，由 adapter 的 start/stop/service evidence 驱动 `up_running/down_running`；未绑定 adapter 时明确报告 `ADAPTER_MISSING`。
- [x] 冻结 reference TX / feedback RX 相关门禁：sequence、frame CRC、schedule CRC、时间戳顺序、feedback timeout、硬件 latch 标志和 `<=100 ns` 分辨率必须同时成立。
- [x] 建立与业务 payload 解耦的 32 B `TdmaTransportFrame`：固定小端 wire layout、SHORT/LONG、origin/sequence、schedule/ring CRC、identity CRC、hop count/limit 和 transport CRC。
- [x] 为 EtherCAT-style 飞行短帧增加 `FLIGHT_MUTABLE` slice 更新契约：identity 只绑定不可变路由身份，segment owner CRC/version 保护局部数据，transport CRC 随 hop/内容更新。
- [x] 将 RefMem realtime binding 内帧限制到 260 B：36 B RefMem header + 最多 224 B critical delta；总线无关协议仍保留 292 B 理论上限，更大 delta 必须进入分片或 background/bulk 路径。
- [x] 建立 `TdmaProcessImageMap` C 契约与 host validator：segment owner、payload class、offset、length、flags 和 map CRC；拒绝重叠、越界、重复 ID、非法 owner 和状态/命令策略冲突。
- [ ] 将 `TdmaProcessImageMap` 编码为正式 System Pack 表并接 DeploymentGate；运行态 generation、dirty mask、target 和 segment CRC 属于 process image 段头，不写入静态 map。
- [ ] 实现 process image active/shadow 双缓冲：domain task 只写 shadow，core1 只在 cycle boundary swap，PIO/DMA 只读 active。
- [ ] 冻结 compact VDC flight segment 和 critical RefMem flight segment wire format；当前 216 B VDC 诊断帧不能成为最终 process image。
- [ ] core1 TDMA runtime 同时服务 `TDMA_UP_LEG` 和 `TDMA_DOWN_LEG`。
  - 进行中：ring runtime 双向 service 已就绪；PIO SPI ring adapter 已绑定，`up/down_running` 由 adapter 驱动，物理双向钩子待接入。
- [ ] 空闲无业务 payload 时持续发送/接收 `IDLE_BEACON` 或等价 freshness 帧。
  - 进行中：`tdma_pio_spi_ring_adapter` 已在每次 service 构建/发送 `IDLE_BEACON` 短帧并解析 RX（含 beacon 计数）；板端物理 TX/RX 钩子待接入（P0.5-3）。
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
  - 进行中：`tdma_pio_spi_ring_adapter` 定位为 bring-up transport adapter，物理钩子可替换；尚未绑定为唯一架构承载。
- [x] PIO SPI adapter 只解析 `TdmaTransportFrame`，不得再校验或假设 `refmem_sync_frame`；VDC、RefMem、OTA、SD、LOG 内帧由各域自行验证。
  - 完成：`tdma_pio_spi_ring_adapter` 只编解码 `TdmaTransportFrame`（IDLE_BEACON 短帧），不接触 RefMem/VDC 内帧。
- [ ] PIO SPI adapter 实现 RX/TX 重叠的 byte/block cut-through：只修改本节点获授权 segment，测量每 hop pipeline delay；未取得实测证据前仍标记 store-and-forward bring-up。
- [ ] BISS-C adapter 作为后续类 IP 核，提供编码/解码、timestamp、CRC 和 quality。
- [ ] UART / RS485 adapter 明确 MTU、latency、timeout 和降级质量语义。
- [ ] 所有 adapter 复用同一 TDMA payload/window/completion contract。

## P7 - HIL 验收

- [ ] 两板最小系统同时 UP/DOWN 常驻 5 min。
- [ ] 验收 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=true`、`WINDOW_BOUND` 不作为最终态、`BAD_FRAME=0`。
- [ ] 复测 RefMem AUTO NodeLoad 双向同步，确认 ACK/重发/fence 不依赖偶然窗口命中。
- [ ] 复测 VDC observation，确认 DPLL accepted sample 可追溯到 TDMA ring evidence。
- [ ] 扩展 3 节点、5 节点 profile，验证只扩表不改算法。
