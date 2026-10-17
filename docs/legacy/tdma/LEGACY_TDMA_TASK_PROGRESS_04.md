# TDMA 任务进度归档

Status: Frozen
Domain: tdma
Canonical: `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_04.md`
Related: `docs/tdma/TDMA_TASK_PROGRESS.md`
Last updated: 2026-10-17

## 归档说明

由 C14 轮转从 `docs/tdma/TDMA_TASK_PROGRESS.md` 迁入，按时间正序保存最旧条目；未删除、未改写。

### TDMA-PROGRESS-20260914-008 - RESET 有效位退休候选撤回与 SD 失败保全

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与源码撤回完成，不计生产优化收益，完整目标继续。
- 延续 007 的 RESET 首拍线索，候选在既有 Core1 seqlock 内用私有有效位退休记录，
  Core0 只在成功复制并复验 guard 后规范化自己的副本。RESET 不再清整个 snapshot，
  每拍 work 初始化、generation、公开计时格式与全部探针保留。有效位必须独立于
  sequence：回绕后的 sequence=0 仍可能是合法记录，不能据此删除。
- 软件回归为 62 passed；确定性混合动作与前序原实现进行 100000 步逐字节 snapshot
  差分一致（本轮验证快照，非事实源）。覆盖 RESET 延迟/合并、空记录、有效峰值、
  回绕和旧 scheduler notification 取消；该回放不构成并发内存模型或硬件 WCET 证明。
  首次 host 命令引用不存在的测试文件，exit=4、零测试；修正入口后的原始日志均保留。
- 候选 A/B/Boot 构建通过。ARM/map 快照（非事实源）：RESET 路径减少一次 1464 B
  snapshot 清零，360 B work 清理保留；私有有效位增加 1 B 静态占用并使用既有布局
  余量，heap 外仍为 88 B，不能称为零 RAM 占用。PIO headers 逐字节相同。首次符号
  审计误选其他模块同名局部符号，exit=1；按所属 recorder 对象定位后通过，非链接失败。
- 下表为同 UID、实际槽位下两轮保留的完整外层峰值范围（us，有限窗口快照，非事实源）；
  基线来自 006 的恢复源码双槽窗口，候选来自本轮。不同窗口不是同拍配对，也不是 WCET：

  | 节点/选拍/槽位 | 基线两轮范围 | 候选两轮范围 |
  |---|---|---|
  | NO1 RUN A | 608.852–630.380 | 648.416–692.724 |
  | NO1 RUN B | 618.580–619.512 | 651.200–701.984 |
  | NO2 OTHER A | 706.908–726.904 | 724.344–736.124 |
  | NO2 OTHER B | 710.148–725.988 | 715.820–726.816 |
  | NO3 OTHER A | 667.440–692.212 | 720.708–729.276 |
  | NO3 OTHER B | 692.184–708.308 | 692.456–719.108 |
  | NO4 OTHER A | 669.632–683.868 | 673.756–685.012 |
  | NO4 OTHER B | 650.508–676.316 | 663.580–687.812 |

- 全部组的候选最大值高于该基线；与紧邻 007 恢复包的同 UID/槽位比较，仅主站最大值
  下降，也不能建立稳定的完整收益。因此撤回候选，不等于已证明候选导致增长。候选
  第一槽主站两轮高峰为 sequence=860/1318，均非 RESET 首拍；body 分别为
  676.580/631.432 us，外围为 25.404/19.768 us（同拍快照，非事实源）。RESET 批量
  初始化不是常规高峰的主要解释，007 的外围差值不能全部解释为 memset 或扣减门禁。
- 候选 `slot-r2` 与原参数补采 `slot-r3` 均在 NO3 保存失败：storage job 为
  `FAILED,FILE_WRITE`，error=6 对应 `STORAGE_MANAGER_ERROR_WRITE_FAILED`；接收字节
  与预期一致、CRC 相符，读回目标文件却为 0 B（本轮失败快照，非事实源）。两轮
  RAM 导出、计时及 STOP/config ACK/许可证退出有效，其余三板 SD 字节相符；不能将
  取证命令 exit=0 或另开 epoch 的 release 标志写为原 SAVE 通过。
- 两个 SD 失败窗口的全部 PEAK/RUN/OTHER 均保留，包括 `slot-r2` 的 NO3 OTHER
  749.384 us（有限窗口快照，非事实源），没有因保存失败而丢弃高值。只软复位 NO3，
  核验同 UID/端口/build 后，`normal-r2` 普通闭环、`slot-r4` 补采和 `normal-r3`
  普通恢复的 STOP/SD 均通过；没有擦卡、格式化或删除文件。SD 写失败根因尚未隔离。
- 候选 P3 的本轮 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` 严格检查通过，仍不是正式产品
  P3 或完整预算验收。候选源码已归档并撤回，恢复源码相关回归为 62 passed（本轮
  验证快照，非事实源）。恢复 A/B/Boot 与四板 OTA、校准、普通环路检查通过；恢复
  P3 流程完成但 strict=false，仅保留总流程耗时 462.835 s 超过 450.000 s 的拒绝
  （诊断快照，非事实源），不放宽时限或提升正式产品验收声明。
- 恢复包两轮完整外层峰值如下（us，有限窗口快照，非事实源；实际启动槽位已与
  候选包绑定，不是同拍配对）：

  | 节点/选拍/槽位 | 第一轮 | 第二轮 |
  |---|---|---|
  | NO1 RUN B | 618.868 | 607.964 |
  | NO2 OTHER B | 725.000 | 719.976 |
  | NO3 OTHER B | 706.792 | 688.868 |
  | NO4 OTHER A | 666.824 | 647.780 |

- `restored-residual-review-r1.json` 保留全部 PEAK/RUN/OTHER，并对同条记录的互不
  重叠子区间作差。主站两轮高峰走 READY 结果接受分支，RX_PARSE 为
  229.660/203.708 us；从 adapter 扣除 prologue、handoff、overlay prepare/boundary
  与 status 后，余下 164.152/173.536 us。从站高峰均走 capture/request 分支，
  RX_PARSE 未执行，adapter 余下 36.684–41.608 us（同拍分项快照，非事实源）。
  主站下一步分解 `tdma_pio_spi_ring_origin_service()` 中的 observe/publish 和 RX
  接受；从站继续 capture/request、latch 及准备准入归因。这些差值包含控制工作、
  探针及可能的运行干扰，不是已经证明的可节省时间，不可重复加到父项或跨角色混算。
- 恢复包两轮 START 后零查询、STOP 后导出并核对 SD 字节；自主窗口原始
  passed/closed_loop/realtime=false、diagnostic=true 保留。最终普通闭环、四板
  STOP/config ACK、许可证退出及 SD 字节核对通过；没有新增 SD 写失败。当前 build
  为 `20260913212611`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本轮快照，
  非事实源），无生产实现差异；NO5 未操作、registry 未变，完整预算、正式 RAM、
  逐圈物理节拍、SD 失败根因及增长因果仍开放。
- 原件入口：`candidate-source.json`、`differential-review-r1.json`、`linkage-review-r1.json`、
  `phase-comparison-r1.json`、`latest-baseline-comparison-r1.json`、`decision-r1.json`、
  `slot-r2-sd-inspection-r1.json`、`slot-r3-sd-inspection-r1.json`、`no3-reset-r1.json`、
  `rollback-source-r1.json`、`restored-checkpoint-r1.json`、`p3-restored-r1/diagnostic.json`、
  `restored-slot-review-r1.json`、`restored-residual-review-r1.json`、`review-final-r1.json`；证据根为
  `out/HardwareAcceptance/20260914/tdma-flight-timing-reset-retirement/`。

### TDMA-PROGRESS-20260914-007 - 时钟快速路径候选撤回与 RESET 首拍归因

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与源码回退完成，不计生产优化收益，完整目标继续。
- 延续 O2/O4 的时间戳读取/重装归因，候选在 VDC 时钟初始化时缓存精确整数 ns
  比例，让 `vdc_timestamp_clock_now_ns()` 驻 SRAM；每次仍读取新 timer ticks，
  分数周期与零频率保留通用换算。初始化、epoch、回绕和全部计时探针语义保持。
  候选两版与恢复源码的相关 host 回归各为 25 passed（本轮验证快照，非事实源）。
- 首次链接失败且未 OTA：初始化被内联入 SRAM 入口，跨越接收 workspace 的
  对齐边界，使 BSS 后移并溢出 RAM。失败源字节、map 和日志保留；显式禁止冷路径
  内联后，A/B/Boot 构建通过。ARM/map 快照（非事实源）：read/now 分别为 40/36 B，
  init/通用换算为 160/124 B；新增静态 4 B，heap 外余量由 88 B 变为 84 B。
  PIO 产物逐字节相同，没有释放 PIO 空间或改变 wire 布局。
- 候选与前序恢复包分别按板卡及实际启动槽位对应；下表为两轮窗口保留的完整外层
  峰值范围（us，有限窗口快照，非事实源；不是同拍配对，不代表 WCET）：

  | 节点/选拍/槽位 | 基线两轮范围 | 候选两轮范围 |
  |---|---|---|
  | NO1 RUN A | 608.852–630.380 | 605.704–659.596 |
  | NO1 RUN B | 618.580–619.512 | 652.188–666.036 |
  | NO2 OTHER A | 706.908–726.904 | 712.336–731.268 |
  | NO2 OTHER B | 710.148–725.988 | 720.976–730.516 |
  | NO3 OTHER A | 667.440–692.212 | 704.512–709.968 |
  | NO3 OTHER B | 692.184–708.308 | 702.560–725.060 |
  | NO4 OTHER A | 669.632–683.868 | 701.844–745.520 |
  | NO4 OTHER B | 650.508–676.316 | 684.860–695.832 |

- 各组候选最大值均高于基线；在没有完整收益证据且增加 RAM 的情况下撤回候选。
  这不等于已经隔离代码布局、缓存/总线/IRQ 或实际工作的增长机制。全部选拍和
  PEAK/RUN/OTHER 原件保留 `TDMA_SERVICE_TIMING_VERSION`、stage ticks/calls、
  UID、build、包哈希、槽位、镜像大小/CRC，不能将嵌套分项重复求和。
- 候选 `slot-r3` 的 NO1 RUN 为 RESET 后 sequence=1：外层 666.036 us、body
  558.656 us，body 外为 107.380 us（同拍快照，非事实源）。代码确认
  `tdma_service_timing_phase_begin()` 在 body 起点前清整个 snapshot；外围还含
  正常 work 初始化、发布和边界读取。因此该差值不是实测 memset 时间，不允许
  扣减预算。下一切片先分解 RESET 初始化与发布，再验证按已有失效标志有界退休；
  必须保持 Core1 所有权、Core0 单次快照、generation、空记录输出和全部探针语义。
- 候选第二槽位的 `slot-r2` 在 NO1 临时授权应答处超时，reset generation 缺失，
  分析正确拒绝；原失败与 STOP/SD 原件保留。原参数补采 `slot-r3` 通过证据核验，
  没有覆盖失败或增大超时。采样 START 后零查询、STOP 后导出；自主窗口原始
  passed/closed_loop/realtime=false、diagnostic=true 保留，普通恢复不提升自主段。
- 候选 P3 的本轮 quick diagnostic 严格检查通过；恢复源码的同范围流程完成但
  strict=false，保留 NO3 OPMode APPLY 应答超时、TRN-01 SCK、TRN-03 重装余量
  拒绝和验收总流程超时。二者均不是正式产品 P3 或完整预算验收，不能关闭历史
  间歇故障。恢复 A/B/Boot 构建通过，heap 外余量回到 88 B（map 快照，非事实源）。
- 恢复包两轮主站 RUN 外层为 729.632/636.056 us；NO2 OTHER 为
  704.008/712.456 us，NO3 为 695.396/668.400 us，NO4 为 680.344/661.588 us
  （有限窗口快照，非事实源）。主站高值为 sequence=1376，body 695.652 us、
  外围 33.980 us；其中 owner 611.804 us，嵌套 RX handoff 257.084 us、
  RX parse 229.080 us，不能重复求和。该拍不是 RESET 首拍，说明 RESET 初始化
  不是全部增长的解释，撤回候选也未关闭波动。按实际槽位配对及全部分项已保存；
  两轮 STOP/SD、最终普通闭环、STOP/config ACK、许可证退出及 SD 字节核对通过。
- 当前恢复 build 为 `20260913202953`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本轮快照，
  非事实源）。候选源码快照与恢复源码分别绑定各自构建和凭证；无生产实现差异，
  NO5 未操作，registry 未变；完整 500 us、正式 RAM 和增长因果仍开放。
- 原件入口：`candidate-source.json`、`link-failure-r1.json`、`phase-comparison-r1.json`、
  `reset-first-phase-r1.json`、`rollback-source-r1.json`、`restored-checkpoint-r1.json`、
  `p3-restored-r1/diagnostic.json`、`restored-slot-review-r1.json`、`review-final-r1.json`。

### TDMA-PROGRESS-20260914-006 - latch 初始化候选撤回与同包跨槽位对照

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与回退闭环完成，无生产实现差异，不计优化收益。
- 延续 O2/O4 的读取/重装分解，候选只将禁用状态下 RX/TX latch 的
  `put UINT32_MAX → PULL → MOV X,OSR` 改为 `MOV X,~NULL`。FIFO 清理、SM restart、
  PC、epoch-before-enable、许可/状态检查和所有探针保持。两处 CPU helper 的静态
  指令分别由 103→92、73→62（ARM 快照，非事实源）；PIO 程序字节相同，原 latch
  不消费 OSR。候选与恢复后的相关 host 回归各为 32 passed，A/B/Boot 均构建通过。
- 初始“候选→旧包→候选”的同探针对照中，NO2 OTHER 外层候选四轮为
  723.396–736.432 us，旧包两轮为 699.760–710.348 us（有限窗口快照，非事实源）。
  没有稳定的完整收益，候选已撤回；该决定不等于已经证明改动造成增长。
  原候选源码字节、patch、build、失败与采样均保存，不能用恢复源码替换候选证据。
- 进一步核对发现初始三批 NO1–NO3 槽位为 B→A→B，NO4 为 A→B→A（启动快照，
  非事实源）。源码和槽位同时变化，原对照不能隔离代码效应；候选还移动了其他
  函数布局。主站保留的 RUN 峰值中 RX/TX latch 读取/重装均未调用，主站增长不能
  直接解释为这些 helper 的单次执行成本。缓存、总线和 IRQ 干扰尚无隔离证据。
- 恢复源码重新构建、真实 P3 后，用同一个包在两槽位各采集重复窗口。以下为
  各窗口保留的完整外层峰值范围（us，有限窗口快照，非事实源；不是同拍配对）：

  | 节点/选拍 | 第一槽位/两轮范围 | 另一槽位/两轮范围 |
  |---|---|---|
  | NO1 RUN | A：608.852–630.380 | B：618.580–619.512 |
  | NO2 OTHER | A：706.908–726.904 | B：710.148–725.988 |
  | NO3 OTHER | A：667.440–692.212 | B：692.184–708.308 |
  | NO4 OTHER | B：650.508–676.316 | A：669.632–683.868 |

- 同包各节点的两组范围重叠，当前不能建立固定的槽位增量，也不能证明槽位无影响。
  每个样本绑定 UID、build、包哈希、active slot、启动来源槽、镜像大小和 CRC；
  `SYSTem:OTA:SLOT?` 读取的是 metadata，须与 `SYSTem:OTA:RES?` 及包内 image 对应。
  COMMIT 后的 confirmed slot 查询保留原值，不假设同步更新。全部保留拍的
  `TDMA_SERVICE_TIMING_VERSION` 与枚举分项、ticks/calls 原样存档，不混加嵌套项。
- 本轮采样 START 后零查询、STOP 后导出，板端记录及 SD 逐字节核对通过；自主
  采样原始 passed/closed_loop/realtime=false、diagnostic=true 保留，普通恢复通过
  不能提升整段自主窗口。候选、旧包复查和最终恢复各轮的 STOP/config ACK、许可
  退出和普通闭环均留证。初次旧包 OTA 后误用运行后收尾检查，因许可 UNAVAILABLE
  与未加载矩阵而失败；原失败保留，另行只核验配置前的硬件 STOP 与 config ACK，
  不伪造许可失效，也不放宽运行后的原收尾检查。
- 候选 P3 的 coarse CLK topology 响应超时保留，strict=false；恢复源码本轮
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` 的 failures 为空、strict_gates_passed=true。
  此字段只描述该诊断范围，不是正式产品 P3、完整 WCET 或历史间歇故障的关闭证明。
  恢复 A/B 的 heap 外余量仍为 88 B（map 快照，非事实源），正式 RAM 未通过。
- 最终 build 为 `20260913192602`，源码指纹恢复为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本切片快照，
  非事实源）。默认凭证绑定恢复源码，候选未进入生产；NO5 未操作，registry 不变。
  后续按实际槽位和镜像分组，继续隔离 RX/latch 有效工作与布局、缓存/总线/IRQ
  干扰，不能凭静态指令减少、某轮低值或重刷恢复宣布完整 500 us 达标。
- 原件入口：`candidate-source.json`、`rollback-source-r1.json`、`layout-review-r1.json`、
  `slot-review-restored-r1.json`、`p3-restored-r1/diagnostic.json`、`review-final-r1.json`。

### TDMA-PROGRESS-20260914-005 - 自主 origin 有限 service 屏蔽实板验证

- 日期：2026-09-14
- 状态：PARTIAL；主站有限 service 主体屏蔽和 DMA 进展验证完成，完整目标保持进行中。
- 新增 `CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT`，经既有 Calibration 易失
  许可证与 TDMA owner 准入。SCPI 仅发布试验意图；owner 等待自主态稳定后，在
  `tdma_component_core1_service()` 主体之前执行有限门控，跳过 physical、owner、
  RefMem publish 和 training gate。屏蔽时只读授权与 DMA 档案版本，不读 FIFO、
  不收割 RX、不选取新 payload、不重装 PIO/DMA；没有借用 OTA skip 路径。
- 等待和跳过次数、区间检出上限来自 `tdma_origin_blackout.h` 的
  `TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS/SKIP_CALLS/MAX_INTERVAL_US`。撤销、STOP、配置
  或时钟代际变化、硬件不就绪及超限会取消试验，恢复既有 owner STOP/退休路径。
  上限在后继 service 入口检查，不是独立硬件 watchdog 的强制恢复保证。终态快照
  使用版本保护；完成后立即退休 DMA 保留档案，新的准入会替换旧试验快照。
- 两轮实测快照（非事实源）：分别连续跳过四次完整 service 主体，CPU 边界区间为
  4023.392/4068.688 us，DMA 档案发布版本分别从 132→140、130→138；各保留四个
  区间内完整返回，sequence 为 587–590、588–591。epoch 与双端 sequence 一致，
  transport checked、capture/output remaining、RTT present 和 fault 检查通过。
  SCPI 在 START 后零查询，STOP 后统一导出；各窗口 SD 字节核对通过。
- 第二轮记录 local generation 25→26（快照，非事实源）。屏蔽前已发布的待选 shadow
  可以在屏蔽后由 DMA 选取，因此不能要求整个区间 generation 恒定；当前 watermark
  快照没有绑定屏蔽前的 pending selection 租约，本轮不声明新的 generation 准入证明。
- 本轮证明有限主站区间内硬件继续完成传输；CPU 边界时间与旧 `FORMAT_RTT` 档案
  不能证明逐帧物理抖动、绝对 VDC 时间、全部从站同时屏蔽或任意长度的逐圈保全。
  屏蔽区间的低 CPU 耗时不作为完整 phase 的优化收益。
- 未屏蔽对照快照（非事实源）：主站 RUN 外层为 622.952/620.976 us；NO2 OTHER
  为 691.924/704.320 us，NO3 为 692.900/700.740 us，NO4 为 678.896/679.576 us。
  当前与前序保留拍并非同拍配对，完整 500 us 仍未达到，增长因果继续开放。
- 软件快照（非事实源）：十项许可证/屏蔽回归及十二项档案、构图取消、DMA 和 NO5
  隔离回归通过；真实包装函数验证整个主体跳过与恢复 STOP。首次 host 编译因测试桩
  static 声明冲突失败，修复后通过，原日志保留。
- 首次 A 链接失败：诊断慢路径被内联入 SRAM 快速入口，跨越对齐边界导致 BSS
  后移。失败源字节、map 和日志保留；明确禁止慢路径内联后，A/B/Boot 构建通过。
  ARM 快照（非事实源）：入口 24 B 驻 SRAM，慢路径 440 B 驻 Flash，新增静态记录
  64 B；workspace 保持原地址和大小，A/B heap 外余量由 152 B 变为 88 B。PIO 产物
  字节相同，正式 RAM 未通过，没有扩大预算、超时或存储区域。
- 当前源码 P3 首轮在 NO4→NO1 邻接漏检处失败。独立复核检出四链路，但继承串口
  默认读取粒度与 P3 不同，不能据此关闭因果；恢复原 P3 时序的第二轮拓扑通过，
  仍有 TRN-01 SCK、TRN-03 重装余量及短帧启动屏障拒绝，诊断流程完成、strict=false。
  首轮清理报告因校准前与基线矩阵比对而未通过，四板实际 STOP/配置确认/许可证失效
  均已记录。进入屏蔽前补做同矩阵普通短帧闭环通过；最终普通恢复、STOP 和 SD 通过。
- 当前 build 为 `20260913182559`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本切片快照，
  非事实源）。NO5 未操作，registry 状态不变；后续继续从站证据路径、完整 WCET、
  严格校准、绝对时间、generation 准入及正式 RAM 闭环。
- 原件入口：`blackout-r1-review.json`、`blackout-r2-review.json`、`linkage-review-r1.json`、
  `p0t-recheck-context.json`、`p3-r2/diagnostic.json` 与 `review-final-r1.json`。

### TDMA-PROGRESS-20260914-004 - 连续计数器压缩与记录池复用核算

- 日期：2026-09-14
- 状态：PARTIAL；离线指令与 ARM 布局核算完成，固件及板端状态未改变。
- 新方案压缩计数器自身，保留生产 DATA 字节路径。使用 SM wrap 处理高电平路径
  的 X=0，下降路径零值直接落入 LOW，配合 delay 保持所有路径同样的计数节拍。
  指令模型快照（非事实源）：由 10 条减到 8 条，每次轮询仍为五个时钟；当前时钟下
  派生量化为 20 ns，不代表绝对时间精度。
- 验证快照（非事实源）：继承的 1460 个波形、200 个双计数器回绕用例通过；新增
  35840 个电平序列/初相/X 初值组合逐项比较旧、新汇编程序，轮询时间、计数值、
  FIFO PUSH 和丢失记录完全相同。错误 delay 与 wrap 负测被拒。首次 LOW 伪记录、
  短脉冲漏采、FIFO 满和 blocking PUSH 破坏映射的反例继续保留。
- 空间快照（非事实源）：自主 origin 若替换旧 RTT 与保留 latch，TX catalog 可从
  32 条变为 30 条；已有 RTT 本来由 DMA 重装，不能算新 Core1 收益。follower TX
  的候选占用为 18 条，但保持现有 DATA 时 RX 仍需 36 条，不能直接安装；不能
  因共享程序可运行就静默迁移 RX 端点到 TX。实际资源、入口及 persona 生命周期
  仍须按 `tdma_state_machine_resources.h` 和 program manager 重新核验。
- ARM ABI 快照（非事实源）：采用当前固件编译选项，单独编译尺寸对象并核对当前
  map；workspace/origin 为 7712 B，service 为 7696 B，现成 follower 余量仅
  16 B，后方存在 480 B 链接对齐填充。旧 origin TX 数组 1232 B，follower 双计划
  2368 B；前者仅 MASTER/FLIGHT_ORIGIN 路径使用，后者公共入口要求
  SLAVE/FLIGHT_PROCESS_FOLLOWER，为进一步互斥复用提供依据。
- 独立布局原型快照（非事实源）：保持 RX ring 起点，嵌套 union 复用上述互斥数组，
  放入两路各 32 word、按 128 B 对齐的原始环及 1024 B 不透明记录预算，workspace
  为 7808 B，较当前增加 96 B；原始环/记录预算偏移为 6528/6784 B。原型没有进入
  固件，记录预算不是冻结结构，现有对齐填充也不是可用 RAM 凭证；必须用真实实现
  重新链接 A/B，核验地址、代码驻留、STOP/DMA/Core0 退休及消费期限。
- 自主 origin 的既有 AL3 executor 可作为双 FIFO 收取候选，避免直接新增两路 DMA；
  但必须同时退休旧暂停/清 FIFO/重装路径，并证明每圈边沿数量、实际 CONTROL 身份、
  完整 capture/trailer 和版本一致。`tdma_origin_observation_t` 与现有
  `TDMA_ORIGIN_RECORD_FORMAT_RTT` 不能被静默改作绝对计数器；需要明确记录格式、
  getter/解析器及 VDC/Calibration 时钟映射。不把计数器入环等同于特等席交付。
- 首次 ARM 尺寸命令因 Windows 引号解析和缺少 build-job 头失败，原日志保留；
  修正命令解析及审计源后，当前布局和独立原型均编译通过，未扩大超时或运行预算。
- 本轮仅离线汇编、指令执行及 ARM 尺寸对象，无新固件构建、OTA 或 P3 声明；
  当前源码指纹仍为 `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`，
  板端最后验证版本仍是 `20260913163720`。完整 WCET、严格 P3、正式 RAM、实际
  service blackout 与逐圈绝对时间/身份保全继续开放；registry 状态不变。
- 原件入口：`working-design.txt`、`edge-model-r1.json`、`equivalence-r1.json`、
  `workspace-audit-r2.json`、`workspace-audit-r3.json` 和 `review-final-r1.json`。

### TDMA-PROGRESS-20260914-003 - follower PIO 共尾压缩反例与回退

- 日期：2026-09-14
- 状态：PARTIAL；候选已拒绝并回退，取证与恢复闭合，长期目标继续。
- 目的：为连续边沿计数腾出 follower RX 指令空间。候选用 Y 区分普通第八位与
  末位，共享采样及 ISR/FIFO 保全尾部；同步调整 terminal PC 白名单、WAIT 补丁和
  byte 重装计算，不增加 DMA、SM 或缓冲池，不安装连续计数器。
- 软件与构建快照（非事实源）：候选 DATA 程序由 28 条压到 22 条，含旧 latch 的
  follower RX 由 32/32 降到 26/32；代价是完整 byte 重装项由 7 增到 8 个时钟。
  离线候选 216 项、生产候选流水线等 232 项、资源/DMA 34 项通过；C 计划覆盖六、
  八节点容量分别 122880/163840 个 ownership/alignment 用例。ARM 测试另有两个
  既有 fixture 偏差：缺少 node-count 参数及禁用 staging 时仍预期调用 map；两次
  失败、候选内修正和最终 1 项通过均留证。修正随拒绝的候选一并归档，生产测试恢复
  原版，该既有测试缺口仍需后续处理。
- 否决依据：`tdma_pio_spi_clkdiv_for_baud()` 与 `tdma_origin_cadence_calculate()`
  使用硬件可表示的分频。当前配置的离线快照（非事实源）为 divider256=1066、
  每 bit 六个 PIO 周期，实际 bit 间隔包含 24/25 个 `clk_sys` 时钟；DATA delay=15
  时候选完整 byte 需要 25 个时钟。`fractional-probe-r1.json` 覆盖 128 个分频初相、
  每相两帧，原版相位偏差为零，候选出现 692 个晚一个时钟的采样；当前频率下对应
  4 ns。此为指令模型反例，不是实板测得的抖动。只在固定整数 bit 周期下通过不能
  证明实际分频相位保持，不用缩减指令空间的收益抵消此偏差。
- 候选构建快照（非事实源）：A/B/Boot 通过，build `20260913171122`，源码指纹
  `f69799cafb1400dc788b1637ff0ca70ea151e28eea9f64eaa93828654403dad2`。
  仅 follower DATA 的 PIO 指令字改变，其余程序含自主 origin 一致；A/B link-free
  各 2200 B，含 2048 B heap 预留，额外各 152 B，没有新增 RAM。候选源码原始字节、
  patch、生成头、构建和模型保存在证据根，回退不删除这些原件。
- 候选当前源码 QUICK P3 流程、短帧及普通恢复通过；strict=false，TRN-01 SCK
  gate 和 TRN-03 SCK row 选择未通过，原始拒绝单列保留，不归因于 DATA 模型反例。
  两轮自主整段 passed/closed_loop/realtime=false、diagnostic=true；后段各板
  accepted 增长，rejected/missing 无增长，不能据此提升整段稳定性或采样相位保证。
- 完整外层对照（us，有限窗口快照，非事实源；前序 002 与候选各两轮，同矩阵、
  四邮箱及 v8 探针；NO1 使用 RUN，其余使用 OTHER，不是各分项独立最大值）：

  | 节点 | 前序 R1/R2 | 被拒候选 R1/R2 |
  |---|---:|---:|
  | NO1 | 650.544/681.388 | 646.196/649.388 |
  | NO2 | 697.232/711.556 | 728.840/702.072 |
  | NO3 | 714.424/697.948 | 705.000/695.772 |
  | NO4 | 732.284/676.340 | 691.716/672.764 |

  完整预算仍未达，变化不一致；本次没有消除 latch 的逐圈读取/重装，不声称 CPU
  固定收益，前序增长峰值及因果缺口保留。
- 恢复：四板经 STOP、OTA 回到 build `20260913163720`，固定矩阵普通闭环通过，
  STOP/config ACK、许可证失效和 SD 逐字节核对通过；生产源码恢复前序指纹
  `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`。
  候选 P3 凭证只绑定候选，不替换恢复版本凭证；本轮仅提交文档记录。
- 下一边界：先保持最短实际 SCK 间隔下的采样相位与 byte 余量，再评估连续计数器
  自身压缩或观察性边界工作迁移。新的 FIFO 独占交接、逐圈身份/时间记录、静态
  缓冲期限及 VDC 时钟映射仍须一起证明。完整 WCET、blackout、正式 RAM 和 registry
  状态不变；原件入口为 `review-final-r1.json`、`rejection-decision.json`、
  `fractional-probe-r1.json`、候选 P3/记录/SD 和 `rollback-normal-r1`。

### TDMA-PROGRESS-20260914-002 - 锁内空队列选择提前结束

- 日期：2026-09-14
- 状态：PARTIAL；空队列行为和有限实板对照完成，完整预算及严格 P3 未通过。
- 范围：`tdma_traffic_scheduler_select_impl()` 在既有锁内完成周期刷新与普通队列
  过期清理后，若普通队列及 recovery depth 均为空，直接记录原 GATE_CLOSED、
  无 traffic class，释放锁并返回；跳过后续 VDC/recovery/RefMem/控制队列扫描。
  recovery depth 包含在途帧，非空时继续原路径；不提前读取无锁队列，不省略统计，
  不改派发输出、准入和 STOP 边界，不新增计时探针。
- 软件：调度器 host 回归及计时回归 24 项通过（验证快照，非事实源）。新覆盖包含
  空拍预算刷新、结果和输出保留、同周期新数据准入与恢复帧在途；既有过期清理、
  恢复重试/预算、优先级、STOP 取消及重新开放继续通过。
- 构建快照（非事实源）：A/B/Boot 通过，build `20260913163720`，源码指纹
  `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`，
  源文件数 1044。实际汇编在刷新计时后进入空分支并跳转至结果记录、解锁和返回。
  A/B link-free 各 2200 B，含 2048 B heap 预留，额外余量各 152 B；无新增 RAM，
  PIO 产物字节一致。保持六节点编译容量、四邮箱运行布局与同 v8 探针。
- 同矩阵分项对照（us，有限窗口快照，非事实源；前序 067 四个当前版窗口、候选
  两个窗口；均取每板 RUN/OTHER 外层峰值所在拍，分项不是独立最大值）：

  | 节点 | 前序 SELECT_EMPTY 范围 | 候选 SELECT_EMPTY 范围 | 候选外层 R1/R2 |
  |---|---:|---:|---:|
  | NO1 RUN | 27.116–40.432 | 11.168–20.692 | 650.544/681.388 |
  | NO2 OTHER | 28.940–48.184 | 25.312–26.544 | 697.232/711.556 |
  | NO3 OTHER | 33.696–48.816 | 30.296–31.680 | 714.424/697.948 |
  | NO4 OTHER | 29.800–39.424 | 28.420–29.492 | 732.284/676.340 |

  各板候选保留拍中的 EMPTY 分项均低于该板前序四个记录，但刷新成本本身也有波动，
  不能将区间相减当作固定节省。完整外层未一致改善，主站第二轮高于前序本轮窗口；
  原 NO3 789.932 us 高峰继续保留，增长因果未解决，完整 500 us 未达。
- 当前源码 QUICK P3 完成，短帧通过；strict_gates_passed=false。粗 CLK 校准中
  NO2 的 `TOPology 4,1,2` 应答超时，coded marker gate 同轮未通过；原始拒绝保留，
  不用流程完成或后续普通闭环替代严格门禁。两轮自主整段
  passed/closed_loop/realtime=false、diagnostic=true，后段 accepted 增长，
  rejected/missing 无增长；有限后段不提升为整段稳定性。
- 取证流程失败与恢复：首个编排脚本将诊断退出码 1 当作中断，提前进入恢复；
  恢复的 recorder ARM 命令超时，读回仍为首轮冻结 epoch，未建立新记录。
  `sequence-command-r1` 和 `recovery-normal-r1` 原件保留。随后独立 STOP 复核，
  对板上首轮记录执行实际 SAVE/读回，与主机原件逐字节一致，补齐首轮 SD 证据；
  新的继续脚本保留诊断失败并完成第二轮及普通恢复，没有修改退出门禁或延长期限。
- P3、两轮记录及最终普通恢复均完成 STOP/config ACK、许可证失效与 SD 核对。
  START 后零查询，实测 payload 与接收长度维持固定四邮箱。最终当前固件普通模式
  passed/closed_loop=true。完整 WCET、正式 RAM、逐圈特等席、blackout 和增长因果
  继续开放；下一步聚焦 RX 交接/接受及 latch 重装等主成本，避免仅凭局部优化宣称达标。
- 原件：`source-checkpoint-r1.json`、`comparison-r1.json`、A/B 选择函数反汇编、
  `p3-r1/diagnostic.json`、两次编排命令、恢复失败、全部记录/STOP/SD 及 `normal-r1`。

### TDMA-PROGRESS-20260914-001 - O6 前后固件峰值重复性对照

- 日期：2026-09-14
- 状态：PARTIAL；有限对照和恢复完成，时间增长因果未解决，完整预算未达。
- 范围：生产源码不变，以 067 当前版、066 基线版、067 恢复版各两个窗口执行
  B/A/B。重核两版封存 manifest、原 OTA 包和 build，沿用同一四邮箱矩阵、板端
  记录窗口与 v8 探针；START 后零查询，STOP 后导出及 SD 逐字节核对。
- 外层峰值（us，R1/R2；有限窗口快照，非事实源；NO1 取 RUN，follower 取 OTHER，
  各板各窗口各自选拍，不是同步跨板观测）：

  | 固件/次序 | NO1 RUN | NO2 OTHER | NO3 OTHER | NO4 OTHER |
  |---|---:|---:|---:|---:|
  | 当前 067，前置 | 663.396/654.184 | 701.596/713.220 | 692.884/710.052 | 696.372/701.456 |
  | 基线 066 | 668.980/675.264 | 716.092/723.888 | 726.332/704.352 | 681.680/686.080 |
  | 当前 067，恢复 | 671.352/664.472 | 726.776/723.756 | 789.932/709.992 | 701.248/755.336 |

- 主站前序 714.560 us RUN 高值未在本轮当前版窗口重现，但 NO3 高峰重现，NO4
  也出现较高值。不能宣布增长消失、排除回归或确认 O6 算术的唯一因果。主站
  OTHER 外层三组依次为 725.176/729.352、685.668/708.396、485.580/576.388 us；
  低值不覆盖原高值，全部 body 与分类外层记录保留。
- 口径：PEAK 按 `total_ticks` 选 body 峰值，RUN/OTHER 按 `full_phase_ticks`
  选外层峰值。PEAK 携带的外层值不是外层最大值保证；预算按分类外层复核，并保留
  `SCHEDule` 对完整 WCET、deadline 和后置分类/调度发布开销的验收责任。
- 布局复核：实际 A/B 的 RX 接受、flight unload、receive health、traffic select、
  timing record 和 adapter service 六个函数，地址及抽取的指令字在两版完全相同。
  每个目标共比较 2927 个唯一符号，10 个地址变化、3 个符号指令字变化（构建快照，
  非事实源）。这不覆盖 literal/data、缓存、总线或中断行为，不能由此确定共享
  干扰的因果；扩展 RX impl 的补充分析与首轮分析分别保留。
- 六个自主窗口整段 passed/closed_loop/realtime=false、diagnostic=true，原始
  拒绝保留；后段 accepted 增长而 rejected/missing 无增长，不提升为整段稳定性。
  两次四板 OTA 均通过，最终当前 build 为 `20260913154535`。每轮 STOP/config ACK、
  许可证失效及 SD 核对通过，普通模式恢复 passed/closed_loop=true。
- 当前源码指纹仍为 `814e12c7c49a5fe39856d4f2542e83c3030de3d46b8eda6c69740b4f23dcc3e1`，
  沿用 067 绑定同源码的真实 QUICK P3（strict=true）；本轮没有新生产实现或新 P3
  凭证，旧版运行仅作为明确标注的对照。正式 RAM、完整 500 us、逐圈特等席和
  blackout 继续开放。
- 原件：`plan.json`、`repeat-comparison-r1.json`、`layout-analysis-r1.json`、
  `rx-hot-layout-r1.json`、两次 OTA、六轮记录/STOP/SD 及 `normal-r1` 恢复证据。
  后续在不改变工作量与门禁口径的条件下，检查共享干扰、高频探针自身成本、RX
  接受和空队列调度，实际收益须重新实测。


## 归档索引

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_03.md` | TDMA-PROGRESS-20260913-067..TDMA-PROGRESS-20260913-030 | 38 | 2026-09-19 |
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_02.md` | TDMA-PROGRESS-20260912-029..TDMA-PROGRESS-20260912-001 | 29 | 2026-09-19 |
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_01.md` | TDMA-PROGRESS-20260911-007..TDMA-TASK-20260817-001 | 51 | 2026-09-19 |
