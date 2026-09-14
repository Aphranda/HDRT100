# VDC 定时命令与共同时间传输方案

Status: Draft
Domain: VDC
Canonical: `docs/vdc/VDC_COMMAND_TRANSPORT_PLAN.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-14

本文是 `VDC-CMD-001` 的待审方案，服务于 `VDC-LONGTERM-001`，不冻结 wire 契约，
不允许据此启用从机控制，也不替代调度、角色、Calibration 和正式锁相门禁。
当前证据与 host 反例见 `VDC-PROGRESS-20260914-004`。

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
   连续性必须有边界。现有 compact phase trailer 继续服务逐圈观测，不能独自恢复 epoch。
5. 映射失效时冻结可信信号输出并降低质量；新映射与命令在 Core1 边界组合，避免因更新
   映射而产生未经约束的相位跳变。重新 ARM 或切换会话不能复用旧锚点。

上述时间模型尚未实现或验收；`COMMON_TIME`、`clock.valid`、主机内部 `LOCKED`
或精确匹配的序列，单独都不足以建立这个映射。

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

`s_tdma_flight_sync.context` 当前只由 compact DELTA 接收路径写入，对外只提供 peer、
mirror 和 quality。建议先完成 `VDC-RESOURCE-001`：将该路径状态收敛为专用 compact
receiver，上述未使用数组从此实例移除，通用 RefMem receiver 保持完整能力。必须用
原实现与新实现的 DELTA 行为对照、当前源码 target link/P3 和短帧原件确认回收结果；
不得先把理论字节数写为固件成果。组装区、稳定命令、guard、预约锚点和双缓冲各自预算
闭合后，再实施命令接线。

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
