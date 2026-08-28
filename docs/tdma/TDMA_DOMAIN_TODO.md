# TDMA 基础件主域待办

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_DOMAIN_TODO.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-08-28

本文档维护 TDMA foundation 的独立待办。这里记录影响上/下行 TDMA、ring runtime、payload registry、adapter、completion、quality、HAOFV system node 和 HIL 验收的事项。

## 文档接口

- 稳定语义和跨域契约：`TDMA_DOMAIN_ARCHITECTURE.md`。
- 当前任务状态和退出门禁：本文件。
- 构建、测试、OTA/HIL、失败与回退证据：`TDMA_TASK_PROGRESS.md`。
- 契约登记状态：`docs/check/DOCS_REGISTRY.md`。

## 状态规则

任务状态只使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`。代码和 host test 通过但尚未
OTA/HIL 的任务不得标为 `DONE`；运行时临时剩余容量不得用于改变静态 payload 布局。

## 已有基线

- TDMA owner、ring runtime、traffic scheduler、双 FIFO、固定 process image 和 raw-flight
  persona 已有代码基线；单次构建/HIL 数值只在任务进度文档保存。
- 校准训练的测量、矩阵和 raw waveform 归 Calibration Domain；TDMA 只拥有 transport、窗口和
  completion evidence。
- 产品 SHORT 使用固定 Node mailbox；VDC 诊断帧不是产品 process-image payload。

## 当前主线

先完成拍级确定性 schedule 和 mandatory-first SHORT process image，再以五板 TDMA-only HIL
冻结 WCET/波形基线；随后逐 phase 加载 DPLL/VDC、RefMem 与最小控制。任何负载回归都修复责任
负载，不放宽 TDMA phase。

## 里程碑总览

| ID | 里程碑 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-M1 | 拍级 schedule 与编译门禁 | DONE | 静态 gate 全绿，五板实测 WCET 不超合同。 |
| TDMA-M2 | mandatory-first SHORT process image | DONE | 固定布局、publisher/parser、CRC、SCPI 与多板 HIL 全闭环。 |
| TDMA-M3 | DPLL/VDC 最小负载 | IN PROGRESS | 基础字段已随固定周期运行；仍需硬件 latch 样本可追溯、节点锁相且发布 VDC。 |
| TDMA-M4 | completion/reliability | PENDING | ACK/fence/retry/fail-closed 与长期错误率门禁成立。 |
| TDMA-M5 | T2 reservation 与控制 | PENDING | 预算内 PREPARE/READY/fence/completion 五板闭环。 |

## 当前任务表

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| TDMA-DET-001 | 拍级 phase table 与唯一时间单位 | DONE | `APP_REALTIME_PHASE_TABLE` 和编译期邻接/周期闭合检查存在。 |
| TDMA-DET-002 | schedule 与最大 wire 编译门禁 | DONE | phase、WCET、SPI 整拍和最大 SHORT wire 超限均拒绝构建。 |
| TDMA-DET-003 | 拍级 runtime/SCPI evidence | DONE | 五板异步 OTA 后已采集两轮 start/runtime/WCET/miss，schedule miss 为零。 |
| TDMA-DET-004 | 拆分 prepare/preload/hardware-launch/wire/feedback | PENDING | 首边沿由 PIO/硬件事件触发，各子 phase 有独立拍级合同。 |
| TDMA-DET-005 | active topology/baud/tail 动态容量门禁 | PENDING | profile 激活前重算 wire，超出 TDMA WCET fail closed。 |
| TDMA-PAYLOAD-001 | mandatory-first Node body 预算与固定布局 | DONE | `tdma_process_image_layout.h`、编译断言和预算工具一致。 |
| TDMA-PAYLOAD-002 | compact VDC/DPLL publisher/parser evidence | IN PROGRESS | 最小字段已上 wire 并通过五板运行计数；量化/饱和及硬件 latch HIL 待完成。 |
| TDMA-PAYLOAD-003 | critical RefMem 与 ACK/fence/quality | IN PROGRESS | baseline delta 与 ACK 摘要已上 wire；待正式 commit/fence 闭环。 |
| TDMA-PAYLOAD-004 | 最小控制 token | IN PROGRESS | 固定 token 已预留；待 owner、opcode 与 completion 接入。 |
| TDMA-PAYLOAD-005 | optional 静态余量准入门禁 | DONE | optional 只使用 mandatory 后余量，layout 不保留 runtime-free 字节。 |
| TDMA-HIL-001 | 五板 TDMA-only WCET/频率/占空比/SD 波形基线 | PENDING | OTA 后原始波形、SVG、schedule snapshot 与零错误基线归档。 |
| TDMA-HIL-002 | 逐 phase 开载且 TDMA 零回归 | PENDING | 依次启用 VDC/DPLL/RefMem/control，TDMA deadline/error 不增加。 |
| TDMA-DPLL-001 | PIO/DMA hardware latch correlation | IN PROGRESS | PIO/DMA TX completion 与 clock-latch 证据已接入；仍需 active PATH_DELAY 和五板同圈 eligible sample。 |
| TDMA-DPLL-002 | 节点 DPLL lock 与 VDC 发布 | IN PROGRESS | 四节点 TDMA 同时收发和参考反馈已实测；NO1..NO4 仍为 CHECKING，NO5 观测工具已固化，待 eligible sample 后验证指定间隔/同时触发。 |
| TDMA-REL-001 | ACK/fence/retry 和长期稳定性策略 | PENDING | 原始错误率先收敛，再以有界重发/修复完成 EtherCAT-style 验收。 |
| TDMA-REL-002 | 单 Node recovery 双冗余 buffer 与固定预算 | PENDING | 静态 recovery budget、双 buffer 交替填充/发送、每周期最多一帧；构建/DeploymentGate、双/四/八节点 HIL 和超限回退证据齐全。 |
| TDMA-T2-001 | REFMEM + 部分控制后的 T2 最小载荷预算 | PENDING | 不超固定 SHORT/body 和 phase WCET，编译期拒绝 overcommit。 |

## 当前阻塞项

- `TDMA-HIL-001` 尚未执行，因此拍级 phase 和新 wire layout 只能视为代码/host 基线。
- `TDMA-DET-004` 未完成前，CPU phase 仍包住组合 TDMA service，尚不能证明物理首边沿完全不受
  其他负载调用路径影响。
- formal ACK/fence 与 control owner 尚未接入，新布局中的对应字段当前只提供固定基础语义。

## 统一完成定义

任务只有同时满足架构 owner、不变量、编译/pytest、OTA 多板实测、SD 原始波形/分析、失败回退
证据和文档门禁，才可标为 `DONE`。HIL 未执行时必须停留在 `IN PROGRESS`。

## 迁移前历史任务索引（快照，非状态事实源）

以下旧 P0-P7 清单仅保留迁移追溯；当前状态以上述稳定 ID 任务表为唯一事实源，单次证据以
`TDMA_TASK_PROGRESS.md` 为准。

## 产品样板迁移

- [x] ST7735S 保持原生 `80x160`、offset `(24,1)` 扫描，由刷新层把逻辑 `160x80` UI 软件旋转为横屏；产品样板已确认开机界面完整且不再斜切。
- [x] TF 卡按 SPI1 GPIO10/11/12、CS GPIO15、CD GPIO14 完成产品样板检测、FAT 挂载、目录、64 B 读回和 boot snapshot 写入验证；StorageAO 不再占用 LCD 的 SPI0 资源。
- [x] 主页和功能页已切为 `160x80` 单卡片四行布局，旧 `240x135` 渲染器不再进入产品刷新路径；待产品样板视觉确认。
- [x] KEY1/KEY2/KEY3 已接入独立事件层：35 ms 去抖、短按、700 ms 长按和 250 ms 重复；导航按上一项/返回、详情、下一项映射，待产品样板手感确认。
- [ ] 完成产品样板 KEY、SMA OUT1..4、SMA IN1..4、ISO1452 DE/`/RE` 以及 TDMA BiSS+RJ45 单跳/闭环 HIL。
- [x] 增加产品板单板 RJ45 输出到输入的只读回环工具；开路基线 build `20260818151639` 已确认 TX 递增、RX=0、10 MHz 和 CS/CLK/DATA pin profile 正确。
- [x] 产品板 build `20260818154324` 完成单板网线回环：两个连续 15 s 窗口中
  UP/DOWN 同时运行，第二轮 TX/RX 各增长 7230 帧，adapter/phys bad、magic fail、
  overrun 均零增长。当前 `TIMESTAMP_MISSING` 单独归入 P0.5-4/5，不否定电气/数据回环。
- [x] 产品差分单板完成 15/20/25 MHz 阶梯；25 MHz build `20260818155435`
  的 60 s 窗口 TX/RX 均增长 29721，bad/magic fail/overrun 零增长。该结果先作为
  速率裕量证据，不直接替代长线缆、EMC、温度和多板产品验收。

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
  - 完成：`tdma_pio_spi_phys` 常驻物理层已改为**半双工单环**（每板下行 TX master + 上行 RX slave 两个独立 SM；实测对称接线：发送端闲置 RX/CS=`21`、TX/DATA=`23`、CLK=`24`，对端闲置 TX/CS=`16`、RX/DATA=`18`、CLK=`19`）。ring adapter 有 REFERENCE/FORWARD role，`set_phys_ctrl`/`set_phys` 连接物理层。
  - RX 可靠性（2026-08-18）：rx_byte SM 重写为 pico-examples 标准 **autopush 模式**（in_shift autopush threshold=8），根治手动 X 计数器导致的字节边界漂移（坏帧从 ~45% 降到 ~0）；DMA 双缓冲捕获 + magic 帧头扫描对齐（EtherCAT 式帧头锁定）；`SYSTem:SYNC:VDC:TDMA:PHYS?` 暴露 rx_bad/busy/magic_fail/magic 对齐分布诊断。
  - 发送（2026-08-18）：reference 由 core1 TDMA service 二分频发送，当前 core1 service 约 1 kHz，因此 bring-up beacon 稳定为约 500 Hz；follower 收到一帧立即逐帧转发。1 kHz 试验显示软件 pipeline 的最坏情况延迟接近周期，暂不作为当前基线。
  - OTA 安全（2026-08-18）：core1 在 OTA 会话期间跳过 TDMA service（`ota_ao_is_active`），RefMem TDMA 维护日志在 OTA 会话中静默，flash lockout poll 保持紧凑，两板 OTA 稳定 PASS。两板烧录 HIL 常驻验证已跑（`tdma_ring_monitor/ring_rate_measure.py`）。
  - HIL 收敛（2026-08-18 build `20260818101157`）：COM5 `91274BA197662714` 作为 reference slot0，COM6 `73E940D75B406BCD` 通过 `SYSTem:TDMA:RING:LOCAL 1` 切为 forward slot1；15 s 只读速率窗口显示 COM5 reference TX `499.7 frame/s`、feedback RX `498.1 frame/s`，COM6 forward RX/TX `499.2 frame/s`，`phys_bad/magic_fail/shift/stall/ring_overrun` 均为 0 增长。
- [x] P0.5-3A：验证 CS/frame-sync 三线单向腿是否消除 1 MHz 方向性丢帧。
  - 完成：下行链路发送端未用 RX/MISO 与接收端未用 TX/MISO 互连线改作 frame-sync/CS；按当前最小系统接线，`GPIO21->16` 为 CS，`GPIO23->18` 为 DATA，`GPIO24->19` 为 CLK。RX PIO 不再无条件连续采样，而是等待 CS 有效后按 SCK 采样；magic 扫描保留为保险和诊断。
  - 实测（2026-08-18 build `20260818072932`）：两板 OTA 后，`COM5->COM6` 与 `COM6->COM5` 都稳定在约 `473.6~473.7 frame/s`，`rx_bad=0`，方向性丢帧明显收敛。
  - 结论：当前最小系统下，CS/frame-sync 三线单向腿比无 CS 连续流更适合 1 MHz bring-up。
- [x] P0.5-3B：TDMA PIO SPI bring-up adapter 速率阶梯与 10 MHz 指标优化。
  - 已完成：在同一接线和同一 CS/frame-sync 物理层下完成 2/5/10/25 MHz A/B OTA 与 15 s 两板 HIL 方向统计。
  - 阶梯结果：2 MHz 双向约 `486.6~486.8 frame/s`、`rx_bad=0`；5 MHz 双向约 `491.0~491.6 frame/s`、`rx_bad=0`；10 MHz 15 s 短窗口约 `490.7~490.9 frame/s`、`rx_bad=0`；25 MHz COM6 出现 `rx_bad` 增长并降到约 `452.7~461.8 frame/s`。
  - 增强诊断结论（2026-08-18）：10 MHz 30 s 窗口仍约 `482 frame/s`，`rx_bad/magic_fail/shift/stall/tx_timeout` 均不增长，说明问题是完整帧漏收或 RX capture 空窗，不是 bit-level 数据损坏。
  - 假锁修正（2026-08-18 build `20260818111944`）：外层 PIO-SPI packet magic 与内层 `TdmaTransportFrame` magic 都是 `54 44`，DMA 扫描指针错过真实外层头时会误锁内层 magic；物理层已增加二级 transport header 校验，要求外层长度与内层 packet size/version/class/header size 同时匹配。
  - 当前 HIL 结论：10 MHz / 500 Hz / core1 service 二分频 / 连续 DMA ring / CS+DATA+CLK 已回到稳定基线；60 s 只读窗口显示 COM5 TX `500.1/s`、feedback RX `498.0/s`，COM6 RX/TX `498.4/s`，adapter `rx_bad=0`，phys `rx_bad/stall/tx_timeout/ring_overrun=0`。
  - 当前结论：以 `10 MHz / 500 Hz / adapter rx_bad=0 / phys overrun=0` 作为后续 VDC/DPLL HIL 基线。1 kHz 升频留到 P0.5-4/5 硬件 timestamp latch 和闭环证据成立后再评估。
- [ ] P0.5-4：冻结并验证最小 feedback correlation：reference TX sequence、feedback RX sequence、identity CRC、schedule CRC、reference TX timestamp、feedback RX timestamp、round trip 和 timeout 必须来自同一圈 ring。
  - 进行中：`TdmaRingRuntime` correlation 逻辑已在，host 回环单测验证 sequence/identity CRC/schedule CRC/round trip 路径成立；两板 PIO SPI HIL 已证明 reference/forward 帧计数稳定接近 500 Hz。当前 TDMA 已接入共享 `timer1/CLK_SYS` 硬件 tick 诊断时间戳，TX/RX timestamp 非零、分辨率约 4 ns，但仍是 CPU 读取时间戳，不是 PIO 边沿 latch，因此物理 round-trip correlation 仍未闭环。
- [ ] P0.5-5：只有当 timestamp 为 `HARDWARE_TICK`、分辨率 `<=100 ns`、带硬件 latch 标志且非 diagnostic-only 时，才允许 `simultaneous_feedback_loop_evidence=1`，并允许 VDC/DPLL 接受该样本。
  - 进行中：runtime 门禁（`<=100 ns`、`HARDWARE_LATCHED`、非 `DIAGNOSTIC_ONLY`）已有 host 单测覆盖（无硬件 timestamp / diagnostic-only 均拒绝）；两板 HIL 当前 timestamp source 已进入 `HARDWARE_TICK` 诊断阶段，`timestamp_resolution_ns=4`、`timestamp_flags=DIAGNOSTIC_ONLY`，最终状态仍为 `TIMESTAMP_MISSING`、`simultaneous_feedback_loop_evidence=0`。下一步必须在 PIO/DMA 边界补 reference TX / feedback RX 真实边沿 latch，再去掉 diagnostic-only。
- [ ] P0.5-6：扩展 HIL 脚本为只读监控 TDMA runtime，不通过串口查询参与续窗；5 min 验收必须记录 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=1`、`BAD_FRAME=0`、`WINDOW_BOUND` 不作为最终态，并在 `docs/temp/vdc_long_monitor/` 输出 summary + SVG。
- [ ] P0.5-7：P0.5 闭环通过后，再进入 DPLL 参数、水位和 reject 策略优化；在此之前 DPLL 曲线只能作为 leg/self-test 诊断参考，不能作为产品闭环质量结论。
- [ ] P0.5-8：细分 `SYSTem:SYNC:VDC:TDMA:PHYS?` 中的 RX 扫描诊断：把当前 `rx_magic_fail_count` 拆成 candidate reject、idle scan miss、real magic miss 或等价字段，避免把“二级 header 拒绝假锁”误读成线路 bit-level 坏帧。
- [ ] P0.5-9：完成校准训练所需的 TDMA transport/persona 集成；第一阶段测量流程、bracket、
  四主结果、质量分类和后续缩窗输入统一由校准域文档维护。TDMA 只判断 transport 是否完成，
  默认零 `PATH_DELAY`、空时钟发送成功或 `ring_adapter_started=1` 都不得视为校准有效。
  - [ ] P0.5-9a：定义 `STOPPED/PREPARED/RX_ARMED/CAPTURE_ACTIVE/RESTORING/FAILED`
    transport 非阻塞状态机，以及 train epoch/seq、当前 master、persona、窗口、raw evidence
    index 和 transport failure snapshot；TDMA owner 是这些执行事实的唯一 writer。
    - 进行中：`SYSTem:TDMA:RING:TRAIN` 已改为 core0 -> core1 原子 command slot，adapter/PIO
      只由 core1 TDMA owner 调用；物理训练结果通过 `clk_train_guard` seqlock snapshot 发布，
      SCPI 状态查询不阻塞 owner。`CALCULATE/VALID/RELOCKING` 属于 Calibration Domain，
      不再作为 TDMA transport 状态扩展。
  - [ ] P0.5-9b：实现 PIO/DMA SPI CLK 基础训练模式。所有节点先 ARM 独立 RX CLK；非主
    节点执行 RX CLK -> TX CLK 逐边沿再生；主节点只注入一次 burst 并终止返回 burst；
    增加 pulse count/chunk/gap/limit 与超时保护，禁止阻塞 core1 或形成无限时钟循环。
    - 进行中：PIO follower forwarding、master autonomous burst、返回首边沿捕获、硬件
      overlap 顺序判定、pulse limit 和返回超时已完成；START 前会重建普通 DATA/CS persona。
      chunk/gap marker 与返回 pulse count 归 P0.5-9c，尚未完成。
  - [ ] P0.5-9c：完成 TDMA 对校准训练的集成。详细 marker、码本、raw correlation、
    第一阶段 bracket、四主结果、bias/residence、统计和 HIL 门禁已迁移到
    `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`，TDMA 只跟踪 transport
    acceptance 和资源执行状态。
    - [x] P0.5-9c-1：码本工具和测试已迁为
      `tools/calibration_ring_validate/calibration_clk_codebook_eval.py`；TDMA 不再保留同名
      测量入口，golden vector 与阈值由校准域待办维护。
    - [ ] P0.5-9c-2：实现 coded TX/RX persona 的 PIO/SM/DMA resource claim，并由
      DeploymentGate 校验 channel、DREQ、buffer capacity 和 instruction/SM 使用量。
    - [ ] P0.5-9c-3：实现有界 buffer、RX/TX DMA 预装、FIFO/IRQ 清理和同步启动；发布
      capture origin、DMA transfer count 与 raw evidence index，不在 TDMA 解释 delay。
    - [ ] P0.5-9c-4：将校准域的 accepted/rejected 结果通过 guarded/seqlock snapshot
      投影给 TDMA，并在 topology/profile/schedule/calibration generation 变化时标 stale。
    - [ ] P0.5-9c-5：训练失败统一恢复普通 DATA/CS persona 并停在 STOPPED；训练流程不得
      自动 START，SCPI/host 不得参与实时相关或续装窗口。
  - [ ] P0.5-9d：接收校准域发布的 raw edge/evidence index、quality、accepted/rejected、
    `rx_window`、guard、timeout 和 freshness；TDMA 只做 snapshot 投影与 schedule/capacity
    gate，不参与 residence/path-delay 计算。
  - [ ] P0.5-9e：实现短 TRAIN frame 的 TDMA transport，按同一 train seq 关联 bounded
    frame、DMA completion、ACK/commit 和失败原因；第三阶段的 `CLK`/`DATA` 双向同时对比、
    四时间戳方程与校准结果由校准域维护。
  - [ ] P0.5-9f：按校准域提交的 master sequence 和 active topology 提供四主轮换窗口，
    保存唯一板卡地址、transport counters、窗口命中和 persona 状态；不使用 COM 号作为
    拓扑键，也不在 TDMA 内解释跨主 bracket 差异。
  - [ ] P0.5-9g：把校准域的 acquisition/feedback timeout、RX window、guard 和 generation
    接入 TDMA schedule；绑定变化或 freshness 超限必须拒绝运行并回到 RELOCKING/STOPPED。
  - [ ] P0.5-9h：保持 TDMA 的可观测边界，只发布 transport/窗口 evidence；per-link delay、
    aggregate/cumulative delay 和双向测距结论由校准域发布。
  - [ ] P0.5-9i：固化 host 工具的 TDMA 编排路径：`STOP -> APPLY -> clear -> ARM ->
    TRAIN_PREPARE/ACK/commit -> TRAIN -> publish/restore -> STOP`。工具只编排维护态，
    不用 SCPI 轮询参与实时转发、相关或时间戳生成；后续 START 由调用者显式触发。
    - 进行中：`tools/calibration_ring_validate/calibration_clk_train.py` 已固化第一阶段四主轮换、ARM 状态
      回读重试和 UTF-8 JSON/CSV/summary；bracket 解释、供电 A/B 快照和校准评分只写入
      校准域方案与任务记录。
  - [ ] P0.5-9j：增加 TDMA integration/unit/HIL 门禁和故障注入：PIO/DMA resource conflict、
    TX busy、RX stall、DMA overrun、ACK/commit miss、window miss、persona 恢复、master 掉线、
    拓扑/profile/calibration generation 变化和默认零表拒绝；训练失败全环回退最后一个 VALID
    profile，并保留 transport failure evidence。

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
- [x] 固定首版 8 × 32 B SHORT process image 和 slot 内 8 B 快速头；core1 生成 RX segment bitmap，core0 只解析命中 slot，2/3/4/8 板只改变 active mask。
- [x] RX bitmap seq16 去重采用 classify/commit 两阶段；只有 RX descriptor 入队成功才提交，FIFO 满时允许同 mailbox 重试。
- [ ] 将 `TdmaProcessImageMap` 编码为正式 System Pack 表并接 DeploymentGate；运行态 generation、dirty mask、target 和 segment CRC 属于 process image 段头，不写入静态 map。
- [ ] 实现 process image active/shadow 双缓冲：domain task 只写 shadow，core1 只在 cycle boundary swap，PIO/DMA 只读 active。
  - 进行中：TDMA owner 已提供双槽 TX image FIFO，core1 在完整 cyclic frame 边界锁定或复用一个 generation；active map 在 STOP 状态 staged、adapter start 时按 local slot 激活。正式 System Pack map 表和 domain dirty publisher 尚未接入。
- [ ] 冻结 compact VDC flight segment 和 critical RefMem flight segment wire format；当前 216 B VDC 诊断帧不能成为最终 process image。
- [ ] 将 T2 reservation/READY-NACK/fence/completion 四类语义段登记到 System Pack
  `TdmaProcessImageMap`；segment 只定义 owner、offset、capacity、flags 和完整性策略，TDMA 不解析
  Trigger 业务字段，精确 wire layout 必须经过契约登记与交叉审核后冻结。
- [ ] 接入 Trigger shadow publisher 与 TDMA active/shadow boundary。
  core0/Trigger domain 只能发布下一 generation 的 opaque segment；core1 在 cycle boundary 原子切换，
  READY/fence/completion 只允许对应 owner slot 写固定 slice，禁止业务代码改 active image。
- [ ] core1 TDMA runtime 同时服务 `TDMA_UP_LEG` 和 `TDMA_DOWN_LEG`。
  - 进行中：ring runtime 双向 service 和 PIO SPI physical callback 已接入；role-specific flight persona
    已实现 reference DMA/burst 与 follower PIO 透明 byte pipeline，ring adapter 不再执行 follower
    的第二次 software TX。四板 raw-flight HIL、固定 segment 在线替换、WKC/尾部 CRC 和
    process-image FIFO/map apply 闭环尚未完成。
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
- [ ] 为 T2 预约发布有界 transport token 和质量计数：encoded、queued、window-open、sent、received、
  validated、returned、fenced/completed，以及 prepare lead time、window wait、forward latency、late、
  deadline miss、retry、NACK 和 timeout；计数只描述运输，不替代 Trigger 业务结论。
- [ ] 定义 reservation/READY/fence/completion 的丢帧策略。
  PREPARE 或 READY 丢失只允许在 arm guard 前有界重发；NACK、CRC、generation mismatch、window miss
  或 fence timeout 必须 fail closed，不得跳过 fence ARM；completion 丢失可重传 evidence，不得重复执行动作。

## P5 - VDC Observation Evidence

- [ ] 冻结 ring frame timestamp evidence：reference TX、每 hop RX/TX、feedback RX、schedule CRC、frame CRC、timestamp source/resolution/flags。
- [x] 冻结两板首版 reference TX / feedback RX 最小相关结构和只读 snapshot；多节点逐 hop evidence table 尚未完成。
- [ ] TDMA observation window 产生 `HARDWARE_TICK / <=100 ns / !DIAGNOSTIC_ONLY` 样本后，VDC 才允许 DPLL accepted。
- [ ] 软件时间戳、host 耗时、单向 leg self-test 只能作为 diagnostic evidence。
- [ ] 长监控末端输出 summary + SVG，区分 leg monitor、TDMA ring runtime 和 VDC lock quality。

## P6 - Adapter 迁移

- [ ] PIO SPI adapter 只作为最小系统 bring-up adapter，不能成为架构绑定。
  - 进行中：`tdma_pio_spi_ring_adapter` 定位为 bring-up transport adapter，物理钩子可替换；尚未绑定为唯一架构承载。
- [ ] 优化 PIO RX 高速采样相位。当前产品板与线缆组合先以 25 MHz 为默认稳定档、
  30 MHz 为工程高速档，35 MHz 及以上暂不开放；后续将 SCK 输入同步策略和
  EARLY/CENTER/LATE 采样相位做成可选择的实验配置，使用现有 TDMA 频率扫描工具复测
  30/35/40 MHz 的有效回环率、坏帧、magic reject、stall、timeout 和 overrun。只有在
  两板长时间 HIL 与环境裕量验证通过后，才允许提高产品档位上限。
- [x] PIO SPI adapter 只解析 `TdmaTransportFrame`，不得再校验或假设 `refmem_sync_frame`；VDC、RefMem、OTA、SD、LOG 内帧由各域自行验证。
  - 完成：`tdma_pio_spi_ring_adapter` 只编解码 `TdmaTransportFrame`（IDLE_BEACON 短帧），不接触 RefMem/VDC 内帧。
- [ ] PIO SPI adapter 完成两级 flight 门禁：raw byte-level cut-through 的代码路径已接通，待四板
  HIL 测量每 hop pipeline delay；随后只修改本节点获授权 segment，并验证 WKC、尾部 CRC、
  FIFO/map apply 和 core0 拥塞隔离。后一级未通过前不得标记 process-image flight 完成。
- [ ] BISS-C adapter 作为后续类 IP 核，提供编码/解码、timestamp、CRC 和 quality。
- [ ] UART / RS485 adapter 明确 MTU、latency、timeout 和降级质量语义。
- [ ] 所有 adapter 复用同一 TDMA payload/window/completion contract。

## P7 - HIL 验收

- [ ] 两板最小系统同时 UP/DOWN 常驻 5 min。
- [ ] 验收 `up_running=1`、`down_running=1`、`simultaneous_feedback_loop_evidence=true`、`WINDOW_BOUND` 不作为最终态、`BAD_FRAME=0`。
- [ ] 复测 RefMem AUTO NodeLoad 双向同步，确认 ACK/重发/fence 不依赖偶然窗口命中。
- [ ] 复测 VDC observation，确认 DPLL accepted sample 可追溯到 TDMA ring evidence。
- [ ] 增加 T2 预约分发 HIL：依次覆盖单板、2/3/5/8 节点 PREPARE/READY-NACK/fence/completion，
  验证 target mask、generation、最坏 lead time、窗口容量、故障注入和 host 只读监控。
- [ ] 扩展 A0-A7 profile，验证只扩表和容量，不改 flight、fence 与 completion 算法。
