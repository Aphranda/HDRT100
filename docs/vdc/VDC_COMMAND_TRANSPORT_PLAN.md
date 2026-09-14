# VDC 定时命令与共同时间传输方案

Status: Draft
Domain: VDC
Canonical: `docs/vdc/VDC_COMMAND_TRANSPORT_PLAN.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-14

本文是 `VDC-CMD-001` 的待审方案，服务于 `VDC-LONGTERM-001`，不冻结 wire 契约，
不允许据此启用从机控制，也不替代调度、角色、Calibration 和正式锁相门禁。
当前证据与 host 反例见 `VDC-PROGRESS-20260914-004`；自主 origin 输入缺口及
目标模式补测见 `VDC-PROGRESS-20260914-007`。

## 1. 需要解决的实际缺口

| 边界 | 当前事实源 | 对后续接线的要求 |
|---|---|---|
| 邮箱容量 | `TDMA_PROCESS_IMAGE_VDC_SIZE`；`refmem_sync_vdc_command_payload_t` | 现有诊断区不能直接表达完整命令；必须明确编码和完整性，不能补默认代际或接收时间。 |
| 运输到命令区 | `distributed_refmem_tdma_flight_parse_mailbox()` 与 `distributed_refmem_get_vdc_follower_command()` | resident parser 只更新诊断，getter 读取独立命令区；需要显式、受保护的交接。 |
| 跨核读取 | getter 裸复制，`refmem_sync_vdc_receive_frame()` 和 context reset 写同一区域 | 发布、读取、清空均要遵守一致性协议；Core1 读取失败有界返回并保留可信输出。 |
| 时间域 | manager 将 `effective_vdc_time_ns` 与本地 uptime 比较 | 必须先建立共同时间映射，再比较到期时间；仅调用有效的默认 identity model 不会对齐不同启动时间。 |
| 序列 | manager 使用普通大小比较，RefMem/Domain 使用模差 | 冻结无效序号、半区间界限、回绕、重复和新 session 的处理；来源 generation 与本地 role generation 分开。 |
| 逻辑时间锚 | adapter 的 `sequence * cycle_period + reference_tx_phase` | 它可作为关联标签；尚不能当作与物理时间等速的绝对纳秒时钟。相位回绕反例必须纳入时间映射设计。 |
| 主机输出生效 | MASTER servo 先改本地 DCO，RefMem 随后发布其快照 | 若命令承诺共同生效时间，主机也要按同一未来时间提交对应输出；发送“已应用快照”不构成同步提交。 |

以下容量数字均为本方案/host 探针快照，非事实源：现有 VDC 区为 6 B，完整旧命令
payload 为 52 B，连同旧 RefMem header 为 88 B。旧版本的 CRC/单目标校验不能通过
伪造一个本地 frame 来证明新 wire 编码已通过跨域审核。

## 2. 时间模型先分清两个映射

候选模型将物理计时和信号控制分开：

```text
本板硬件时间 L_i
  -> M_i：经有向路径校准的 local-to-common 映射
  -> 共同时间 T
  -> S_master：指定主机发布、定时提交的信号模型
  -> 本板信号输出 S_master(M_i(L_i))
```

`M_i` 的建立者是 VDC owner，输入是 TDMA 的成对 hardware latch 和 Calibration 的
有向 delay/bias。它不改写 raw tick，不运行 follower 本地信号 PI，也不提升 formal
quality。`S_master` 的 phase/rate 由配置主机独占；Core1 应用时须进行时间坐标转换，
不能假定不同板的 local tick 比例及 epoch 相同而直接复制 rate/phase 数字。

共同时间轴候选以 TDMA reference 的硬件时基定义，由 session 绑定 reference 身份、
时基版本和 schedule。任意槽位的 MASTER 均在这个共同时间域发布信号命令，TDMA
reference 与 DPLL MASTER 不强制是同一角色。

共同时间的初始化与保持须具备以下过程，具体字段和界限在审核时冻结：

1. STOP/ARM 的 session fence 建立共同会话身份；禁止用各板独立递增的本地 run counter
   假定共同 run 相同。共同 session 的分配、分发/确认及重启唯一性必须有独立证据。
2. 通过既有 mailbox 提前发布未来 reference sequence 的 anchor reservation。各板完整
   验收请求后，由 TDMA owner 在对应硬件事件保留 latch；不依赖 Core0 恰好及时解析。
3. reference 通过同一路径发布该 sequence 的完整时间锚；其他板将自己保留的 RX latch
   与该锚及 directed delay/bias 配对。丢失、错圈、过期、错误 reference/session 均拒绝。
4. 经多锚点确认 epoch、频率比例和不确定度后才发布有效 `M_i`；漂移、有效期、溢出及
   连续性必须有边界。compact phase trailer 须先在自主模式补齐生产和关联，才能服务
   逐圈观测；它不能独自恢复 epoch。
5. 映射失效时冻结可信信号输出并降低质量；新映射与命令在 Core1 边界组合，避免因更新
   映射而产生未经约束的相位跳变。重新 ARM 或切换会话不能复用旧锚点。

上述时间模型尚未实现或验收；`COMMON_TIME`、`clock.valid`、主机内部 `LOCKED`
或精确匹配的序列，单独都不足以建立这个映射。

### 自主 origin 的原始计时前置

当前自主路径尚未提供上述模型需要的时间输入。事实源为
`tdma_pio_spi_ring_origin.inc` 的 `tdma_pio_spi_ring_origin_invalidate_time()`、
`tdma_pio_spi_phys_origin.inc` 的 `tdma_pio_spi_phys_origin_rx()`，以及
`tdma_origin_plan.c` 的 `L_STAGE`：交接时清理旧 clock observation，RX 返回零边沿
时间戳，DMA 每圈清零 DPLL trailer。`tdma_origin_record_t` 的 raw 格式新增 Timer1
与 TX latch 原始字段；
`tdma_origin_observation_t` 仍只描述运输事实。新增 raw 记录没有改变 RX 时间有效性或
DPLL trailer，不能直接提升为绝对时间锚。普通 origin 的鉴相更新不证明自主路径已经
具备这些能力。

下一候选切片按以下边界设计和审核，未完成前不改变 timestamp eligibility：

- TDMA owner 明确发射参考边沿与接收边沿的硬件事件、同圈身份、原始时基和不确定度。
  若用 DMA 读系统计时器，必须给出该读取与真实边沿的偏移/抖动证据及跨字回绕处理；
  “由 DMA 执行”本身不能证明读数就是边沿 latch。
- 先证明本地记录的准备、捕获、发布、退休、缺失及 STOP 取消；保留 source/session/
  schedule 绑定和 sequence 回绕。时间锚的保留不能阻塞下一圈，迟到时明确丢弃。
- 在既有 PIO/DMA owner 和静态容量内核算程序、描述符、FIFO 与 RAM；逐圈 trailer
  与原始边沿关联必须有可执行时序/负测及实板证据。若需要改变跨域格式或语义，先
  完成对应冻结与独立审核；不能借用 NO5 的 PIO/DMA 或增加独立同步帧。
- `VdcSyncAO` 只消费已验证且同圈的描述符；`SyncDpllFB` 仍独占 PI/DCO 应用。
  本地输入闭合后再测自主更新 WCET，随后由既有 mailbox 运输完整时间锚并建立 `M_i`。

#### 当前候选：预留 latch 与 Timer1 的区间关联

`tdma_pio_spi_phys_origin_configure_sms()` 的原型已配置预留 TX latch；其程序仍由
`TDMA_ORIGIN_LATCH_PC` 对应 catalog 保留，并由自主 DMA 图负责重装/使能。原型复用
该程序捕获发车 CS 边沿的首个倒计数，原有 RTT 保留为回传事件的独立原始观测。
TX CS、返回 CS 和 DATA 注入
是不同事件；将它们关联成正式边沿时间仍需验证各自延迟，不能把 RTT 加到估算 TX
时间后直接宣称 RX hardware latch。

在每圈准备时清理旧 FIFO、重装计数器，并在使能 latch 前后分别读取 Timer1 的
`TIMER_TIMERAWH_OFFSET` / `TIMER_TIMERAWL_OFFSET`，以 high/low/high 复验跨字
一致性。DMA 只存原始字；不一致、迟到或缺边沿作为无效记录保存，不能在发车路径
重试到成功。使用 raw 寄存器避免依赖共享 `TIMELR/TIMEHR` 锁存副作用；时钟频率、
初始化代际和本地 capture epoch 必须随记录上下文绑定，本地 epoch 不等于分布式 session。

两个一致的 timer 样本只给出 SM 使能事件的候选区间，还必须证明 DMA/MMIO 顺序、
SM 启动和 GPIO 同步延迟。PIO 高电平倒计数提供相对时间，将这些不确定度一起传播
为边沿区间；不能选择中点后丢弃区间宽度。DMA 向 control FIFO 写发车字时 PIO
可能仍在 guard，因此写入发车字的时间也不能替代实际 CS 边沿。

局部记录先保留原始样本、首个 FIFO 字和缺失标记，再绑定 boundary 的 sequence /
identity / epoch。记录发布必须在生产完成后进行，既有 observation/bank guard、
首尾 sequence、覆盖检查和 STOP 退休要随布局扩展一并复核；候选仅为本地诊断，
不承担共同时间初始化或修改 wire trailer。执行切片见 TODO 的 `VDC-TIME-001` 至
`VDC-TIME-004`，真实 builder 核算及离线模型见 `VDC-PROGRESS-20260914-008`，
集成原型、目标链接和板端预采见 `VDC-PROGRESS-20260914-009`；配置矩阵与 raw
生命周期补测见 `VDC-PROGRESS-20260914-010`。

资源准入以 `TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY` /
`TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY` 为边界，不能使用更大的通用构造上限代替实际
分配。集成原型在独占 sniffer lease 内去掉重复禁用写，并保持各运算显式重设
mode/seed、仅选中 executor 的 SNIFF transfer 参与计算；FAULT/资源释放仍关闭 sniffer。
run 分配保持原边界，literal 分配按对应符号调整。新增归档、producer、冻结副本和
对齐均需计入预算，不能从总 BSS 不变推断没有消耗 RAM。当前容量目标链接和节点
构造矩阵已有证据；扩展矩阵覆盖全部 active mask/有效 local slot 与选定 guard/abort
组合，但使用固定 PIO/DMA、地址及 prefix 几何。raw 复制交错、计数回绕与 STOP 后
退休已有补测，其他目标容量、其余准入几何及有界取消证据仍由 `VDC-TIME-002` 跟踪。

切换缺失的只读审计显示，旧环路停止之后才选择自主 persona、构建并安装 DMA 图；
普通服务与自主图共享 workspace union，不能直接在旧 DMA 运行时覆盖构建。后续
连续性方案须在安全启动边界完成准备，或证明独立 owner 存储与有界交接，保留真实
missing/迟到记录。当前快照只定位到切换区间，不提供精确停发时长；严格 SCK 校准
失败与边沿误差界也须独立闭合，不能通过裁短采集窗口或调整锁相判据绕过。

原始计时格式由 `TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME` 与 `tdma_origin_raw_time_t`
描述；它追加 timer high/low/high、首个 latch 字、FIFO 状态、arm 前 GPIO 输出和
tick rate，不赋予 COMMON_TIME 或 formal flag。缺 latch 的分支必须有界返回，运输
通过与计时有效分开；STOP 导出不能重新给旧样本打时间标签。实测读取区间只证明该次
原始读取，实际 GPIO 边沿、SM 启动、MMIO 顺序和跨域时间映射仍须独立验收。
本候选不授予 timestamp eligibility，也不冻结新的时间契约。

## 3. 固定邮箱的候选承载方式

首选验证完整记录跨多次 mailbox 更新运输。保留全局时间戳 trailer、RefMem、ACK 和
control 的静态分区；仅在已协商的新 mailbox 版本中把 VDC 区解释为记录片段。无新记录
时可重发当前片段；片段推进由 Core0 准备，PIO/DMA 的发车条件不依赖命令是否齐全。

候选片段为“kind/index + group hint + data”，完整命令在 Core0 校验后一次发布。
group hint 只用于组装，不能代替完整 session/record sequence；按来源分离组装状态，
并验证 mailbox 所声明 source 与其物理 segment slot 一致。运输序列、片段序号和主机
命令序列分别计数，不能复用 RefMem 字段的业务 generation 充当命令身份。

候选记录布局快照如下，非事实源、未冻结；所有多字节字段采用显式字节读写，禁止把
wire buffer 强转为带自然对齐的 C struct：

| 候选偏移 | 字段 | 长度 | 用途 |
|---:|---|---:|---|
| 0 | version / kind / source / target mask | 4 B | 区分 session、anchor reservation、anchor commit 和 signal command；完整记录再次绑定来源与目标。 |
| 4 | session identity | 8 B | 分布式共同会话，生成和 fence 协议待审；不是两块板碰巧相同的本地计数器。 |
| 12 | source control generation | 4 B | 发布主机的控制流代际。 |
| 16 | record/command sequence | 4 B | 同来源同 session 内排序；零值、回绕和最大跨度显式约束。 |
| 20 | schedule CRC | 4 B | 绑定冻结调度、拓扑及相关 operating profile。 |
| 24 | reference sequence | 4 B | anchor reservation/commit 或命令所依据锚点的关联身份。 |
| 28 | full reference anchor time | 8 B | 完整共同时间锚，避免只传取模 phase。 |
| 36 | effective common time | 8 B | 命令共同生效时间；非命令类型字段规则另行冻结。 |
| 44 | signal rate | 4 B | 在共同时间域定义的有符号频率修正。 |
| 48 | signal phase | 4 B | 在生效时间处定义的有符号相位修正。 |
| 52 | lock / health / tier / flags | 4 B | 受验证的源状态，不授予 follower 本地 formal 权限。 |
| 56 | maximum lateness | 4 B | 必须同时受接收端已冻结 profile 上限约束，不能由发送者放宽本地门禁。 |
| 60 | record CRC | 4 B | 覆盖前述完整记录，包括身份与时间；算法、覆盖范围和版本待独立审核。 |

该候选为 64 B；在 6 B VDC 区中给控制标签留 2 B、每片传 4 B 时，需要 16 次成功
更新。若使用旧完整 frame 则需要 22 次。这里的更新次数不是硬件环路圈数，也不能直接
乘 Core1 周期当作已经测得的到达上界。各类型记录允许的字段、保留值和 CRC 未冻结前，
该表不得进入产品 parser 或契约登记表。

不采纳的捷径：直接将现有 phase/rate/quality 当完整命令；抢占全局时间戳 trailer；
用 Core0 接收时间补生效时间；通过第二 wire 帧发送命令；从机缺命令时开启本地 PI。
快慢通道压缩可在完整记录闭环后评估，但必须保留完整身份与定时语义，不能用吞吐更高
替代错误来源、旧 session 和过期命令的拒绝条件。

## 4. 提前量、背压与提交规则

生效提前量至少覆盖 `记录片数 × 有界发布间隔 + 环路传播上界 + Core0 解析/重组上界
+ 跨核交接/输出准备上界 + guard`。各项要在当前 build/profile 下测量；目前没有这些
上界的完整证据，不能承诺候选吞吐或正式精度。anchor reservation 同样需要先发布完整
记录，再等待预定硬件事件；超过准备期限就取消，不能改为收到即执行。

Core0 合并尚未开始传输的候选更新；已经开始的记录保持不可变。丢片、乱序、冲突片段、
CRC 错、hint 回绕、超时及 STOP 取消均不得发布半条命令。接收端保留上一有效命令；
Core1 不阻塞等待片段，不在读重试中无限自旋。

MASTER 和 FOLLOWER 均在已选择的共同生效边界提交同一信号模型，并记录 planned /
prepared / applied 身份与实际时间。过期、映射无效或准备未完成时保持可信输出、记录
拒绝并降低质量；不能重写共同时间来“补执行”。源更新比传输快时的合并、限速和控制
带宽须进入闭环稳定性测试，分片运输成功不等于控制稳定。

## 5. RAM 与下一实现切片

前序 target link 余量见 `dpll-four-board-profile/layout-review.json` 及对应构建证据。
不能直接增加按节点展开的组装 buffer 或借用运行中的 DMA buffer。

本轮 native sizeof 和调用点审计得到以下数组占用快照，非已释放 RAM：

| 编译容量 | compact RX 未使用的 ACK/fence/remote-quality 数组 |
|---:|---:|
| 4 | 736 B |
| 5 | 920 B |
| 6 | 1104 B |

`VDC-RESOURCE-001` 已将 `s_tdma_flight_sync.context` 收敛为专用 compact DELTA
receiver，保留 peer、mirror 和 quality，通用 RefMem receiver 保持完整能力。当前
编译容量的实际回收和剩余链接余量见 `VDC-PROGRESS-20260914-005/006`；其他容量
仍只有 host 对照，不能继承目标链接结论。组装区、稳定命令、guard、预约锚点和
双缓冲仍须分别完成预算后再接线，已回收 RAM 不等于这些缓冲已经分配或准入。

## 6. 独立审核与负测清单

| 审核项 | 必须提供的证明 |
|---|---|
| session 与来源 | 同 segment/source/reference；错误目标、旧会话、重启、主机切换及多个来源互不覆盖；会话唯一性和 START/STOP fence。 |
| 完整性与排序 | 逐片 CRC/整条 CRC、缺片/重复/乱序/冲突、group hint alias、完整序列回绕与半区间拒绝；无半记录发布。 |
| 跨核生命周期 | 发布、reset、STOP、读交错及读重试上界；失败时最后可信输出保持；控制计数和诊断计数不混同。 |
| 时间初始化 | 异步启动、reference phase 回绕、预留 latch 退休、缺锚/错锚、频率偏差、时间转换溢出、旧映射失效。 |
| 定时执行 | 主机和从机共同提交；提前量不足、过期、限速/合并、不同静态周期及实际输出边沿对账。 |
| 实时与资源 | 特等席每圈时间戳通道、RefMem 准入和发车连续；目标 RAM/link map、WCET、当前源码 P3；不借用 NO5 或其他 owner 的硬件。 |
| 正式锁相 | 正式 directed delay/bias、timestamp qualification、质量窗口与真实输出测量；控制命令 ACK 和内部 LOCKED 不能代替。 |

审核结论当前为待审；作者不自行将本方案登记为已冻结契约。下一步先闭合资源、调度和
角色基础，补齐 session/时间模型/记录格式的可执行规格，再提交独立审核。
