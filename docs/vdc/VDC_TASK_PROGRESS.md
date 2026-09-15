# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-16

本文只记录当前 VDC 迁移的实施 checkpoint 和证据闭环。任务状态以 `VDC_DOMAIN_TODO.md`
为唯一事实源，稳定语义以 `VDC_DOMAIN_ARCHITECTURE.md` 为准。重构前的长历史记录已移入
`docs/legacy/vdc/`，不再混入当前 checkpoint。

## 文档接口

| 字段 | 规则 |
|---|---|
| `Progress ID` | 每条记录唯一，格式为 `VDC-PROGRESS-YYYYMMDD-NNN`。 |
| `TODO task ID` | 必须引用 `VDC_DOMAIN_TODO.md` 中的稳定 Task ID。 |
| 状态 | 使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；不在本文擅自改变 TODO 状态。 |
| 证据 | 命令、build/HIL、失败、回退和 `out/` 路径只记录已发生事实。 |
| 下一 gate | 必须指向一个 TODO Task ID 或明确外部阻塞。 |

## 当前 checkpoint

最新观察器自身恢复见 `VDC-PROGRESS-20260916-007`：当前源码四板 P3 通过；首次专项
因 NO2 START 应答超时中止，同源新轮次证明三从恢复后在新 epoch 内持续推进序号。
下一步保留同条有效观测及本板时间锚，接通原始反馈，不重启健康 TDMA。NO1 运行中发射记录留存见
`VDC-PROGRESS-20260916-005`：软件、Release、
当前源码四板 quick P3 和 STOP 后同序对账已通过。输出投影准备见
`VDC-PROGRESS-20260916-004`；反馈运输与实际 DCO 应用尚未接通。指定主机接收切片见
`VDC-PROGRESS-20260916-003`。目标整理见
`VDC-PROGRESS-20260916-002`，随后贯通各从反馈和专属校正，再消除漂移、收敛精度。

执行顺序与任务状态统一见 `VDC_DOMAIN_TODO.md` 的“当前主线：四板数据运输与真实闭环”
及其中 `VDC-FLIGHT-001` 至 `VDC-RECOVERY-001` 的任务依赖表；
本文仅追加每个切片已经发生的验证、失败和下一 gate，不复制第二份迁移顺序。

当前执行依赖按 `VDC-PROGRESS-20260916-002` 纠偏；此前各记录的“当前”及
“下一 gate”保留历史含义，不将首帧可用、全窗无错或完整绝对时间映射作为运输前置。

### VDC-PROGRESS-20260916-002：归纳四板数据运输与真实闭环长期目标

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`、
  `VDC-DRIFT-001`、`VDC-PRECISION-001`、`VDC-RECOVERY-001`。
- 状态：IN PROGRESS。用户要求优先让三从收到数据、进入真实闭环，以 internal 快速
  判断应用和漂移，最终复核实际输出；具体任务与依赖已落入 TODO，不冻结新 wire 契约。
- 实现快照，非已验收事实：工作区新增指定 master 的普通 mailbox 稳定接收记录及
  `SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?` 只读查询；未开放命令原型、未接 DCO 应用。
- 作者软件证据：`out/HardwareAcceptance/20260916/vdc-flight-rx-r1/host-review-summary.json`，
  新接收与兼容测试结果待主控随实现切片复核；Release、资源差额和当前源码四板 P3
  尚未完成，不能用旧固件证据替代。独审已报告采集 wrapper 的成功判定和失败恢复
  问题，须修复后再用于专项验收。
- 下一 gate：`VDC-FLIGHT-001` 的独审修正、Release/资源核算、当前源码 quick P3 及
  指定来源/序号/数据一致专项；接收通过后进入 `VDC-FEEDBACK-001`，目标仍保持执行中。

### VDC-PROGRESS-20260916-003：指定主机接收记录与四板专项

- TODO task ID：`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。普通 mailbox 指定主机接收切片已完成软件、构建、四板 P3 和
  接收专项；完整长期闭环仍未完成，以下数字均为本轮快照，非产品事实源。
- 实现：Core0 RefMem 独占接收记录发布，校验来源槽、目标、CRC、READY 和新序列；
  绑定当前角色及已 ACK 的 ring 配置，旧 FIFO 代际不复活。STOP 保留历史、active 清零，
  只读 `SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?` 导出；不开放旧命令原型或应用 DCO。
- 软件/独审：新接收测试 20 项、兼容测试 26 项通过；源码和作者产物 hash 已复核。
  `vdc-flight-rx-r1/host-review-summary.json` 与
  `dpll-flight-receive-r1/implementation-review-r2.json` 保存证据。采集脚本成功判定与
  缺失字段恢复已修复，主控 6 项离线测试及独审 17 项纯模拟检查通过。
- Release：A/B 链接及 Flash 检查通过；map BSS 净增 104 B，链接 RAM 剩余地址空间
  12148 B，不能当作运行栈/堆水位。见 `dpll-flight-receive-r1/resource-review-r1.json`
  和冻结 `after-build/`。源码指纹
  `79069f098382b323b253cc338397f2dc41e34c294dcfea19302db37a76368586`，
  固件包 SHA256 `f0622ddf3dc0e76e08e6debafd6b10b50f4fecc98b252f76955e286f5bf06384`。
- P3：`dpll-flight-receive-r1/p3-r1/acceptance.json` 的 `passed` 和
  `strict_gates_passed` 均为 true，四板均完成新包 OTA；旧 build label 未作为唯一身份。
- 专项：`dpll-flight-receive-r1/capture-r1/input-probe.json` 无采集错误，运行期间查询数
  为零；NO2/NO3/NO4 接收增量分别 2258/2257/2151。三从最终同源 slot 0、mailbox
  序号 3522、CRC 59331、运输序号 6300，phase=-176 ns、rate=2260 ppb；相位/频率与
  NO1 STOP 后 DCO 量化值一致。四板各 20 条板端记录，原生 CRC 通过，SD/RAM 逐字节
  一致。分析见 `dpll-flight-receive-r1/receive-analysis-r1.json`。
- 证明范围：接收增量包含 bootstrap 与自主阶段；本轮证明三从保留记录一致及与主机
  最终相位/频率对账，没有同 mailbox 序号的主机 TX 全字段归档，不声明逐帧完整送达。
  三从实际 DCO 应用增量为零，trace 均无新记录，不据此判断锁相或精度。
- 下一 gate：收敛提交本接收切片；`VDC-FLIGHT-001` 的精确发送版本关联补证可与
  `VDC-FEEDBACK-001` 的观测/命令身份设计共同准备，先完善各从反馈到专属校正的方案。
  必须使反馈体现本从实际 DCO 校正效果；现有 internal 的 clock residual 与 DCO
  输出模型需明确对应，不能用不会响应 DCO 的斜率驱动伪闭环。
- 后继只读审计：`dpll-flight-receive-r1/feedback-next-audit.json` 保存字段、量纲、
  有效条件、输出模型、候选承载、资源估算和测试映射；执行候选落入
  `VDC_COMMAND_TRANSPORT_PLAN.md`，尚未冻结编码或新增反馈控制代码。
- 主控/独审收敛：`dpll-flight-receive-r1/hardware-review-r1.json` 的 40 项核验通过，
  结论为 APPROVE_RECEIVE_SLICE；原生 drop/schedule miss 记录保留，不据接收通过宣称
  全环时序或无丢帧。实现已提交 `3e76053`，提交 hook 核验匹配 P3 凭证通过。
- 上述相对证据路径均位于 `out/HardwareAcceptance/20260916/`，四板已 STOP，串口已关闭。

### VDC-PROGRESS-20260916-004：统一 DCO 输出投影与诊断脉冲计算

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`。
- 状态：IN PROGRESS。本切片准备能反映实际 DCO 校正的输出域残差，尚未接通
  各从反馈、主机独立控制器、专属命令或实际应用；以下数字均为本轮快照，非产品事实源。
- 实现：Domain 新增不可变 DCO 的纯投影和同事件取模相位残差；现有 manager 的
  phase-only 诊断脉冲 deadline 复用该投影，旧 clock 映射保持。调用者仍须提供
  真实本地 RX 时间、对应事件的 NO1 输出相位和当时 DCO 模型，函数不授予样本资格。
- 软件：新增投影与原 Domain 入口共 13 项测试通过，批量覆盖 20433 场景；既有
  manager/replay 兼容 85 项通过。独审以独立大整数 oracle 执行 7404 场景均通过。
  测试范围内诊断 deadline 对应输出整周期的计算误差不超过 1 ns；phase 增加
  300 ns 使计算 deadline 提前 300 ns。这不是 GPIO 精度或实板锁相证据。
- Release：A/B 及 Flash 链接检查通过，BSS 净增零，链接 RAM 剩余地址空间
  12148 B，不代表运行栈/堆水位。源码指纹
  `f4e2a8d6195f966cb82f254acddaf4f14945c47ddc37f8c2d84f426c200408fd`；
  固件包 SHA256 `a81dce6244bfc70397369b0b3e14aef70e24f49d2c8c8fb3df0ae58363b165ba`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 `passed`、`strict_gates_passed`
  均为 true，诊断失败列表为空，耗时 186.343 s；验收范围是四板 TDMA 集成，未验收锁相。
- 接收补测：`capture-r1/input-probe.json` 与 `receive-analysis-r1.json` 均通过；
  NO2/NO3/NO4 接收增量 2190/2244/2208，最终同源 slot 0、mailbox 序号 3506、
  CRC 52363、运输序号 6323，phase=-172 ns、rate=2160 ppb，与 NO1 最终 DCO
  量化值一致。四板各 20 条原生记录通过 CRC，SD 与 SRAM 逐字节一致，运行期间
  查询为零。三从实际 DCO 应用增量仍为零，trace 无新记录，不声明闭环或精度。
- 前序固件的 STOP 后只读尾部诊断：`transport-tail-r1/input-probe.json` 记录
  NO1 准备尾序号 3524，三从最后保留 3522；“最新准备值”不能代替同序发送版本。
  此诊断绑定前序接收固件，与本切片 P3 安装的新包分开，不据一次尾差推导通用历史容量。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-output-projection-r1/`，其中
  `host-review-summary.json`、`implementation-review-r1.json`、`resource-review-r1.json`
  和冻结 `after-build/` 保存原件。命令采用当前 Windows 环境的原生 Python/PowerShell
  入口；提交 hook 使用项目 Git Bash。
- 主控/独审收敛：`hardware-review-r1.json` 的 42 项核验通过，结论为
  APPROVE_OUTPUT_PROJECTION_SLICE；源码已提交 `2e014e1`，匹配 staged 源码的
  P3 凭证及提交 hook 通过。板端 drop/schedule miss 原件保留，不声明逐帧完整送达
  或整表 WCET 已全部闭合；四板已 STOP，采集流程恢复配置并撤销临时许可，串口关闭。
- 下一 gate：推进 `VDC-FEEDBACK-001`
  的真实有效观测、反馈运输和专属校正/应用，精确版本身份随接线闭合。

### VDC-PROGRESS-20260916-005：NO1 运行中原始发射记录留存

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。新增主机本地观测承载，为后续匹配各从反馈提供来源；
  未接远端反馈、控制器或 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：Core1 在既有自主 origin service 内，从已完成的 cyclic raw record 作
  一次有界复制；不读仍由 ARM 改写的 state raw 字段，不消耗原 adapter 观察游标。
  复验发布推进量、epoch/fault/format 和首尾 sequence，物理 owner 核对复制时间界；
  Core0 只读受 guard 保护的独立稳定副本。成功/失败 STOP、persona 或 ARM 退休
  active，历史保留。`READ:CALibration:ORIGin:LIVE?` 在全部 STOP 后读取。
- 软件：helper 六项测试覆盖 16531 场景（含 16020 次逐字节 DMA 交错），通过；
  原 STOP 生命周期及实际 collector/getter/SCPI 序列化共两项通过。独审重跑 helper
  场景和两个集成可执行文件均通过；旧冻结查询输出格式不变。
- Release：A/B 和 Flash 检查通过，BSS 净增 112 B，链接 RAM 剩余地址空间
  12036 B，不代表运行栈/堆水位。源码指纹
  `ca6cdd472cef9f50db9351eaa112e911d631a516f58683941cad15f26847ed40`；
  固件包 SHA256 `91e622b4d52995305ed6d565ca3bc96f55dd2799dbdedbd8769b3ea57bf4cdc2`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  诊断失败列表为空，耗时 181.162 s。P3 范围为四板 TDMA 集成，不含输出锁相。
- 专项：`capture-r1/input-probe.json` 通过，错误列表为空、运行期间查询为零。
  NO1 运行中 copy_count 从零增加到 4051，reject_count 为零，STOP 后 retained=1、
  active=0。最后保留 epoch=1、published_version=12150、sequence=6319、
  identity=753201517；原始记录全部字段与冻结池 age=1 的同序记录相同，冻结尾版本
  12152 减去该 age 对应的版本步数也等于保留版本。三从普通指定主机接收增量
  2199/2254/2179，未产生 origin live 副本或从机 DCO 应用。
- 范围：本轮证明运行中取得完整本地发射记录，不证明它是合格边沿点时间；
  单槽会跳过物理圈次，不构成延迟反馈的同序历史缓存，也不代表特等席反馈已接通。
- 后继路线独审：`../dpll-feedback-event-r1/route-review-r1.json` 支持直接使用各从
  独立 PIO 边沿/线上序列。该入口不消费 DMA packet，因此 DMA 候选到边沿关联不再
  作为前置；真正待接线的是相对 CS 的序列采样配置、内部同事件配对、observer 自身
  退休/恢复、本板时钟锚，以及 NO1 对应事件留存。首样可跳过，不要求全局首帧证明。
- 证据异常：主控误将前序投影基线复制到已存在的 `before-build/`，覆盖旧映射和包。
  全 out 按原 SHA 搜索未找到旧映射副本；旧接收切片报告中的 before-map 原件现在
  不可复核，未改写旧报告或声称恢复。`baseline-path-collision-r1.json` 保留详情。
  本轮资源比较使用独立 `before-current-r1/`，与前序投影 `after-build/` 的 SHA 相符；
  新包、旧硬件采样和本轮验收原件不受该路径碰撞影响。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-origin-live-r1/`；helper 和
  集成证据分别见 `host-review-summary.json`、`host-integration-r1.xml`，实现独审见
  `implementation-review-r1.json`，资源比较见 `resource-review-r1.json`。
- 收敛：`hardware-review-r1.json` 的 54 项检查通过，结论为
  APPROVE_LIVE_ORIGIN_COPY_SLICE；实现提交 `b91ac8c`，staged 源码 P3 凭证和
  提交 hook 通过。四板已 STOP，原始应答确认配置恢复及临时许可撤销，串口关闭；
  原生 drop/overrun/schedule miss 保留，不据本次通过宣称逐帧无损或所有时序已闭合。
- 下一 gate：按 `VDC-FEEDBACK-001` 接入直接
  PIO 观测和逐从反馈，不以本地留存通过关闭反馈或锁相目标。

### VDC-PROGRESS-20260916-006：独立 CS 采样配置与启动重复序号定位

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。完成采样配置和原始帧头诊断；稳态序号专项未通过，尚未接通
  反馈运输、独立控制器或从板 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：新增 STOP-only `SYSTem:TDMA:EVENt:TAP` 和只读 `TAP?`。Core0 在既有
  ring control guard 内检查 STOP/配置 ACK、pending、stopped update 和 geometry，
  发布有 guard 的 SRAM intent；Core1 在 ARM 冻结 prefix/delay/generation，显式
  路径不依赖 DMA alignment 或 overlay 训练。读回分别记录 requested/applied/actual；
  actual_valid 仅表示 PIO/OSR 装载，STOP 后保留历史。默认关闭，PIO 程序保持原样。
- 软件：主控最终 tap/control/geometry/adapter 共 11 项、service/candidate 兼容
  3 项通过；作者相关事件回归 19 项及独审 3 项通过。新 tap 覆盖 160 组实际启动
  组合与边界，service 覆盖尚未 physical start 的 ARM、失败 STOP、失败 START、
  竞争写入及配置恢复。旧提取式 fixture 缺少已有 first-window seams 的失败与修复
  记录保留在 `host-event-summary.json`，不作为生产故障。
- Release：A/B 和 Flash 检查通过，BSS 净增 56 B，链接 RAM 剩余地址空间
  11980 B，不代表运行栈/堆余量。源码指纹
  `5066212ab27d0145de512af70b31676b5008aace9d603232c65abbb44f92bfa2`，
  固件包 SHA256 `274b9dbcbd44c4ca68611a5806b1cb19530c658cc3af21d5110aebb2ca1bcdd7`。
  基线使用前序不可变 after-build 的独立副本，保存逐文件 SHA 对账。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；证明默认配置下四板 TDMA 集成，不代表锁相验收。
- 帧头专项：`header-r1/input-probe.json` 通过。NO2/NO3/NO4 的 CS 相对 prefix
  分别为 24/13/2 bit、WAIT-high delay 为 15 cycle，实际读到完整原始字
  `0x5444A400`，对应 magic 和本次 164 B 包长。重复常量导致旧观察器以 SEQUENCE
  拒绝并退休，符合该诊断预期；主机接收增量为 2173/2225/2198，健康运输继续。
- 序号专项失败：`sequence-r1/input-probe.json` 的 transport_capture_passed 为 true，
  但最终 passed 为 false。三从 requested/applied/actual 均匹配 prefix 120/109/98，
  仅首个空快照 ACTIVE；随后均 joined=1、sequence=1、ordinal=0、fault_bits=0，
  下一次原始序号仍为 `0x01000000`，触发 reason=SEQUENCE，之后永久保持 INVALID。
  该原件支持启动重复序号导致旧观察器退休，不支持稳态连续观测或反馈输入已合格。
  本轮接收增量为 2197/2184/2198，三从 DCO 实际应用增量均为零。
- 两次采集均无流程错误，运行期间 SCPI 查询为零；所有查询/导出在 START 前或
  全板 STOP ACK 后执行。原生记录、SD/SRAM、配置恢复与许可撤销保留原件，
  不以帧头通过覆盖序号专项失败，不由稀疏快照推导逐帧无丢失。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-event-tap-r1/`；资源见
  `resource-review-r1.json`，实现独审见 `implementation-review-r1.json`。
- 收敛：`hardware-review-r1.json` 独立复核当前源码 P3、帧头、失败序号、原生 CRC、
  SD/SRAM 原始字节及动作屏障，结论为 APPROVE_STOP_ARM_CONFIGURATION_AND_HEADER_DIAGNOSTIC_ONLY；
  steady_sequence_observation_passed 仍为 false。实现提交 `084b42f`，匹配 staged
  源码的 P3 凭证及提交 hook 通过。四板已 STOP，requested tap 恢复默认，应用历史
  保留，临时许可已撤销、串口关闭；该提交不表示反馈运输或锁相已完成。
- 下一 gate：`VDC-FEEDBACK-001` 的观察器自身有界恢复；坏样本退休后在后继 service
  重建本地 epoch，保留失败原因，重新从任意稳态有效序列开始，不重启健康 TDMA、
  不恢复全局首帧或 DMA 候选关联前置。完成独立软件与当前源码四板 P3 后，再接
  同事件测量留存、本板时钟锚和反馈运输。
- 后继只读方案：`next-observer-recovery-plan-r1.json` 优先处理显式采样下的
  SEQUENCE 拒绝且无硬件 fault。恢复只动观察器 SM，保留物理 ARM 首次 enable 的
  RXSTARTCUT 原件，不能复用会重读 requested 或覆盖首次档案的完整 prepare/start。
  `next-feedback-measurement-plan-r1.json` 保留后续本板时钟域审计：manager 的
  time_us、TIMER1 tick、逻辑环路时间及现有 DCO 本地锚不可直接混用，不要求完整
  跨板绝对时间映射作为原始反馈运输前置。

### VDC-PROGRESS-20260916-007：启动序号拒绝后的观察器自身恢复

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。为稳态原始反馈输入增加观察器自身恢复；不接通反馈运输、
  主机独立控制器或从板 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：显式 tap 下纯 SEQUENCE 拒绝先保留失败、退休当前 epoch；后继 Core1
  service 分开执行 RESET_PENDING、WAIT_IDLE/start，每次只做一个有界步骤。
  保持同一 ARM 冻结的采样配置，严格递增 observer epoch，epoch 内检查不放宽。
  只重置观察器 SM/FIFO/IRQ，不重新 ARM TDMA、不动 DMA 或转发 SM；首次
  RXSTARTCUT 原始字段保留，当前 epoch 样本字段清空，旧候选不能跨代复用。
- 失效边界：STOP、persona/ARM/tap 绑定或时钟变化、epoch 耗尽及硬件 fault 取消
  恢复。既校验失败相位的 final fault，也在后继重置前复验 sticky stall/bad-PC，
  不把主动 disabled 当故障。其他错误的恢复策略未扩展。
- 诊断：独立 `SYSTem:TDMA:EVENt:RECovery?` 保留累计失败、尝试、启用、延期和取消
  计数、最后失败及物理 ARM/tap 绑定；getter 有界复制，失败保留输出。enable_count
  仅表示重新启用，不是有效样本或 DCO 应用。last_batch_sequence_first 只指向采集
  批次首词，不能与最后发布的 sequence/ordinal 混拼为同次测量。native EVENT ABI 不变。
- 软件：主控最终相关测试 9 项通过；作者相关事件回归 20 项通过，新恢复可执行
  文件覆盖 13 组生产路径，包括多轮启动重复、CS 延期、dirty enable、STOP、绑定/
  时钟/epoch 取消、晚到故障、首次档案及 DMA/转发 SM 不变。独审复跑专项、核对
  生成的生产函数体与源码一致；采集判定 15 个模拟正负例通过。测试发现并修复
  恢复重置前晚到 sticky fault 被清除的问题；FDEBUG W1C 注入时机的 fixture 修复
  及前序失败保留在 `host-event-recovery-summary.json`。
- Release：A/B 与 Flash 检查通过，BSS 净增 148 B，链接 RAM 剩余地址空间
  11832 B，不代表运行栈/堆余量。源码指纹
  `f4b1e5a3709e6e06168d6d23e4d740cfe6cc841e74f5cefe5d9ca830b37957ab`，
  固件包 SHA256 `d1ad89071de8207babbb32acdde40597981cd5e89d813eca3a8c44a2d0faaca8`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；该门禁验证默认 tap 配置下的 TDMA 集成，恢复路径另做专项。
- 专项首轮 `capture-r1` 保留失败：NO2 START 应答超时，底层 helper 返回合成的
  `OK(no payload; verified by state readback)`，实际未执行所称的状态读回；采集器
  拒绝该文本。NO1 尚未 START，随后四板真实 STOP ACK，因此不能据零恢复计数判断
  恢复实现失败。独审见 `capture-r1-failure-review.json`，不将合成文本提升为成功 ACK。
- 同源码、同采集脚本的 `capture-r2` 完成，passed/flow_completed 为 true，errors
  为空，运行期查询为零。三从失败/尝试/启用各增加一次，从 observer epoch 3 恢复到
  epoch 4；各有 19 个 ACTIVE 原生快照，同代 sequence/joined/published 分别推进
  4704/4646/4587，原物理 ARM 首档仍属于旧 epoch。最后失败为 SEQUENCE 且 fault
  为零，STOP 后 pending 为零。主机没有进入恢复，三从主机接收增量为
  2148/2281/2283，实际 follower_apply 增量仍全零；不据此宣称反馈闭环或锁相。
- 恢复 service 部分的累计峰值为 257/213/104 µs，采样 EVENT service 最大值为
  245/401/328 µs；后者在恢复时重置，二者均不是完整 TDMA phase WCET。
  原生记录是稀疏观测，不据连续序号差推断逐物理帧无损。全部 STOP 后导出，
  requested tap 已恢复、临时许可撤销。`hardware-review-r1.json` 独立复核 111 项通过，
  disposition 为 APPROVE_OBSERVER_ONLY_RECOVERY；主控 `main-recheck-r1.json` 再次
  解码四板原始记录并核对 SD/SRAM 全等及恢复判据。原生窗口 TDMA overrun 增量为
  2/1/4/1，NO3 deadline miss 增加一次，三从 RX ring overrun 增量为 30/20/13，
  observation drop 同样保留；本切片不关闭整体时序门禁或授予逐帧无损。
- 收敛：实现与匹配 P3 凭证提交 `dc2ee12`，staged 源码核验和提交 hook 通过；
  文档另行提交，长期目标及逐从反馈任务保持 IN PROGRESS。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-event-recovery-r1/`；软件摘要见
  `host-event-recovery-summary.json`，独审见 `implementation-review-r1.json`，
  资源原件见 `resource-review-r1.json` 和独立冻结的 before-build/after-build。
- 下一 gate：当前恢复专项已完成原件独立复核，分离提交后保留同一完整 event
  record 及本板时间锚，接原始反馈运输，
  不把完整跨板绝对时间映射或最终输出精度作为原始运输的前置。

### 前序实现回顾

`VDC-RESOURCE-001` 当前编译容量的 compact RX 资源切片已闭合，有限采集的 STOP 后
交接通过当前源码四板 quick P3 验证，见下方 `VDC-PROGRESS-20260914-006`。
自主 origin 补测发现时间输入尚未接通，见 `VDC-PROGRESS-20260914-007`；事件与资源
审计见 `VDC-PROGRESS-20260914-008`；原始记录原型、当前容量目标链接及板端预采见
`VDC-PROGRESS-20260914-009`；配置矩阵、raw 退休及两次重臂补测见
`VDC-PROGRESS-20260914-010`；owner 几何与目标容量资源补测见
`VDC-PROGRESS-20260914-011`；其余编译容量链接及真实地址构造、上限容量四板预采见
`VDC-PROGRESS-20260914-013`；RefMem 向量更新的栈/复制收敛与快速验收对照见
`VDC-PROGRESS-20260914-014`；真实构造块取消与复用的软件补证见
`VDC-PROGRESS-20260914-015`；实板暂停构造任务的取消探针见
`VDC-PROGRESS-20260914-016`；交接分阶段计时及两轮自主切换原件见
`VDC-PROGRESS-20260914-017`；有界邮箱合批及当前四板交接对照见
`VDC-PROGRESS-20260914-018`；完整校准 CRC 的 SRAM 实现、初始化失败恢复及普通/
自主相位分离复核见 `VDC-PROGRESS-20260914-019`；就绪阶段合批、构造取消与重臂
交接对照见 `VDC-PROGRESS-20260914-020`；原生记录逐槽离线检查及从板观察缺口见
`VDC-PROGRESS-20260914-021`；RX 同相位补充捕获的失败与回退见
`VDC-PROGRESS-20260914-022`；RX/TX latch 直接初始化及四板对照见
`VDC-PROGRESS-20260914-023`；RX 消费能力和缓冲余量的离线审计见
`VDC-PROGRESS-20260914-024`；TDMA review 06 的证据口径复核见
`VDC-PROGRESS-20260914-025`；RX 站台等待、初次观察及 drop 原因的四板测量见
`VDC-PROGRESS-20260914-026`；最新帧策略的 latch 身份阻断及可执行反例见
`VDC-PROGRESS-20260914-027`；从板连续事件观察的 PIO 原型与边界反例见
`VDC-PROGRESS-20260914-028`；共享采样区 RAM 验收见 `VDC-PROGRESS-20260915-001`，
成对事件计数及有界联合读取原型见 `VDC-PROGRESS-20260915-002`；生产观察器接入与
原生记录复核见 `VDC-PROGRESS-20260915-003`；自主模式观察 FIFO 容量与服务路径
定位见 `VDC-PROGRESS-20260915-004`；最终启用前 CS 准入复验见
`VDC-PROGRESS-20260915-005`；有界原始事件保留基础见
`VDC-PROGRESS-20260915-006`；准入尝试诊断与失败采集自动留证见
`VDC-PROGRESS-20260915-007`；DMA 私有捕获凭据到 RX station 的贯通及 RAM 布局
收敛见 `VDC-PROGRESS-20260915-008`；READY 有界候选查询与原生记录见
`VDC-PROGRESS-20260915-009`，其自主准入拒绝及部分窗口原件保留；foundation 专用
读取与随后获准的完整原生窗口见 `VDC-PROGRESS-20260915-010`；首次 observer 启用
的 DMA 坐标括号及 capture 失效退休见 `VDC-PROGRESS-20260915-011`；后继首帧坐标
证明与启动路线的只读收敛见 `VDC-PROGRESS-20260915-012`；首次 CS 保护及首帧采集
缺口见 `VDC-PROGRESS-20260915-013`；板端有限发帧与最早原始前缀保留见
`VDC-PROGRESS-20260915-014`。当前入口仍为
`VDC-TIME-002`，补齐硬件配置、交接时延
及调度失败证据后，才开放全窗计时验收。按 `VDC-TIME-001` 至 `VDC-TIME-004` 补齐
`VDC-TDMA-001` / `VDC-EVID-001` 的自主时间戳输入，再推进 `VDC-SCHED-001`、
`VDC-ROLE-001`；全表 WCET 和正式锁相仍未闭合，
命令接线须等待契约独立审核。`VDC-TDMA-001`、
`VDC-CAL-001` 和 `VDC-EVID-001` 继续提供正式 evidence；`VDC-SERVO-001/002` 在
正式 evidence 未闭环前的 host/replay 或板端诊断不得用于发布板端目标锁。

本轮按用户要求完成未提交改动对账，先隔离 resident 命令发送并通过普通四板 quick
P3，详见 `VDC-PROGRESS-20260915-026`；后续非法目标位移修复和完整 resident 编译
隔离分别见 `VDC-PROGRESS-20260915-027/028`。每项功能修改后立即 P3；基础流程的
复核不改变上述时间输入、命令应用和正式锁相的未完成状态。隔离验收中重复出现的
粗校准配置拒绝及准备流程有界恢复见 `VDC-PROGRESS-20260915-029`；命令区任务写者
与在线 reset/读交错修复见 `VDC-PROGRESS-20260915-030`；本地角色回切时已接收
命令的退休见 `VDC-PROGRESS-20260915-031`；FIFO 发布入站取消见
`VDC-PROGRESS-20260915-032`；FIFO 借用与 STOP 回收互斥见
`VDC-PROGRESS-20260915-033`；本地 ring 配置绑定与命令取消见
`VDC-PROGRESS-20260915-034`；固定校准配置复测与示波器诊断链路见
`VDC-PROGRESS-20260915-035`；普通循环边界和事件观察器分段计时见
`VDC-PROGRESS-20260915-036`；transport CRC 等价优化及同槽对照见
`VDC-PROGRESS-20260915-037`；事件 lift 快路径及收益边界见
`VDC-PROGRESS-20260915-038`；从板热点 SRAM 放置与完整对照见
`VDC-PROGRESS-20260915-039`。

### VDC-PROGRESS-20260915-039 — 从板事件热点 SRAM 放置，普通运行尾延迟下降

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  038 已提交为 `d238d35` / `54c6fff`。上轮算术简化未证明整体提速，本轮仅将
  `tdma_event_observer_feed()`、`tdma_rx_start_cut_monitor()` 和
  `tdma_rx_dma_counter_observe()` 的函数体放 SRAM；算术、身份/几何复验、
  时间括号、DMB、局部 counter 副本及已退休后的故障累计全部保留。现有 Flash
  helper 仍会调用，不声明关停 XIP 后可运行，也不修改 OTA、预算或命令开关。
- 资源快照，非容量契约：A/B 三段共 4120 B（函数指令共 4118 B），加链接开销后
  data 末端增加 4136 B，利用对齐间隙使 BSS 末端总成本为 4096 B；最终 BSS
  末端 `0x2007c6cc`、C heap 链接跨度 14644 B，不能当实时空闲堆。FreeRTOS
  `configTOTAL_HEAP_SIZE`、两核 scratch 栈边界不变，四板前后 RTOS 读回的
  free/minimum 均为 20344 B。相同函数体并不保证所有外部调用成本相同。
- 软件：85 项生产 observer/start-cut/RX/profile 回归通过，双槽 release 与
  Flash 链接门禁通过。首轮 82 通过、1 个夹具编译失败；恢复原函数声明仍复现
  缺 `tdma_service_timing.h`，补齐该生产头后通过，原失败保留。构建冻结时的
  `d6400ca3…` 指纹早于此纯 host 夹具修复；最终 P3 重新构建并绑定源码 SHA
  `2fdc1c7d9ff13c8c7a30e8a7fc89cf657a7814adf27b3d32fa89a14c2c4addfd`，
  包 SHA 为 `3aba55b99d3be1553c82fc16e9c7d963b91b0f50a8d4e8e3b21d5ac99397239d`。
- 验收失败与恢复分别留证：`event-hot-ram-r1/p3/` 耗时 183.282 s，四板 OTA/
  短帧流程完成，但 SCK 候选不足、无重臂余量合格组合，`strict_gates_passed=false`。
  使用既有 `resume` 和同一包/成功 OTA 记录重做复位、校准、闭环，
  `p3-resume-r2/` 耗时 77.334 s，`passed/flow_completed/strict_gates_passed=true`、
  诊断失败为空；不改门限，不覆盖首轮失败，也不把 77 s 称为完整构建/部署验收。
  为控制槽位，验收前另用冻结的 038 包重装一次，四板通过，额外耗时 106.922 s。
- 性能对照仍固定旧 row35/matrix、板序、完整槽元组和 START 后 RESET 协议；
  038 两轮与新 r1/r3 比较，以下为稳定采样段快照，保留不等长窗口实际分母：

  | 板卡 | 放置前 TDMA overrun/run | SRAM 放置后 TDMA overrun/run |
  |---|---:|---:|
  | NO1 | 0/1669 | 0/1501 |
  | NO2 | 16/1664 | 0/1500 |
  | NO3 | 19/1664 | 0/1498 |
  | NO4 | 24/1666 | 0/1501 |

- 三从板合计由 59/4994 变为 0/4499，支持保留本切片。新 r3 三从板普通 RUN
  完整峰值为 807.664/804.848/796.992 µs，序号均非 RESET 首相位；这些峰值内
  FEED 为 11.296/33.656/17.040 µs，START_CUT 为 4.720/5.268/4.672 µs，
  子段不是独立最大值。新 r1 NO4 的 879.880 µs 为 STOP 过渡，r3 NO1 的
  843.844 µs 为 RESET 首相位，均原样保留，不提升为稳定运行比较。
- 新 r2 的 NO2 STOP 后 OTHER 读回为 `UNAVAILABLE`，夹具误报为 reset mismatch；
  RESET/PEAK/RUN 实际代际一致。原始失败 summary 不改为通过，四份完整 native
  经生产离线判据复核，稳定段无超限，但不补造缺失 profile。r3 仅对 STOP 后
  明确 `UNAVAILABLE` 的只读导出至多重试三次，保存全部响应，完整采集通过。
  `event-hot-ram-after-r1/r2/r3/` 均保留，完整 paired 对照使用 r1/r3。
- 原件复核覆盖两次验收的八份、完整 paired 比较的十六份及失败导出的四份
  native binary，CRC/身份/epoch/完整记录与归档 JSON 一致；稳定时间线逐条
  对应板端样本，三从板 ACTIVE、无故障且 joined/published/elapsed 连续递增。
  最终四板 STOP/config ACK、SELFtest 全零、recorder SAVED；SAVE 不表示 SD
  与 SRAM 字节比对。证据在 `out/HardwareAcceptance/20260915/event-hot-ram-r1/`。
- 下一 gate 回到 `VDC-TIME-002`：先在 TDMA owner 内完成 STOP 清字段前捕获
  几何、全部退休后发布及新 ARM 显式选择，再单独处理自主准备与首次 DMA 发车
  的从板就绪边界，补首物理事件/DMA 帧/完整身份关联。只读路线见
  `vdc-next-time-r1/assessment.json`；未知拓扑不扩成当前四板阻断。
  本轮仅证明普通模式短窗收益，仍为 850 µs TDMA / 1.5 ms 整表预算快照，不能
  关闭自主更新 WCET、500 µs、`VDC-TIME-003/004`、命令应用或锁相门禁。后续
  四路示波器继续以 NO1/CH1 上升沿触发，本轮没有新增波形或正式时间戳资格。

### VDC-PROGRESS-20260915-038 — 事件 lift 等价快路径，尾延迟仍待闭合

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  037 已提交为 `aa6c988` / `cd51d8f`。本切片仅在
  `tdma_event_observer_lift()` 参数校验及 `upper < base` 拒绝之后，增加
  `upper - base < TDMA_EVENT_WRAP_PERIOD` 的唯一候选快路径：`lower <= base`
  时输出 `base`，否则保留 NO_CANDIDATE。这里 `q=0` 表示无需额外完整周期，
  原始计数器跨零的补偿仍在 `base` 中；等号边界和多周期窗口继续走通用路径。
  错误优先级、拒绝时不写输出、溢出检查、整批失败退休及诊断身份均保持。
- 软件与资源快照，非容量契约：生产 C 生命周期和 bigint 差分等 36 项回归
  通过，新增精确错误码、边界两侧、非法参数与输出 sentinel 检查；双槽 release
  构建和 Flash 链接门禁通过。目标反汇编中 RX/TX 的 lift 命中 `q=0` 条件时
  各绕过三次软件 64 位除法，运行命中率未记录；内联后的 FEED 从 3436 B
  降为 3296 B，入口地址不变，主 BSS、
  SCRATCH_Y 与栈底链接末端不变。不把指令变化换算为实测微秒收益。
- 当前源码四板 P3 为 `tdma-event-lift-r1/p3/`，耗时快照 187.310 s；
  `passed/flow_completed/strict_gates_passed=true`，诊断失败为空，四板普通
  process-image/FIFO 闭环通过。源码 SHA 为
  `e59738382c51178a5f0056d049d0bcfd23b88845b348491e09f563bda201c8cd`，
  包 SHA 为 `4b8245caf28e2a99bbc4f0dfda36df5c0e64d3ed9870a87cabea67e4506b0882`。
  为控制槽位，先用既有 OTA 重装冻结的 CRC 固件一次，四板成功，额外耗时
  113.532 s，不计入 P3。OTA 实现、配置及命令运输开关未改。
- 同槽比较沿用 037 两轮 CRC 基线；新两轮约 16 s，使用同一 row35/matrix、
  板序和 NO1/NO2/NO3 槽 2、NO4 槽 1，均在 START 后 RESET、SRAM 记录，
  STOP 后导出并 SAVE。下表为稳定采样段快照；旧 r1 窗口较短，保留实际分母：

  | 板卡 | CRC 基线合计 TDMA overrun/run | lift 快路径合计 TDMA overrun/run |
  |---|---:|---:|
  | NO1 | 0/1502 | 0/1669 |
  | NO2 | 18/1503 | 16/1664 |
  | NO3 | 23/1504 | 19/1664 |
  | NO4 | 18/1505 | 24/1666 |

- **未证明从板整体提速或尾延迟问题解决**：NO4 比例未改善，完整峰值内的
  FEED 子段仍有明显波动。新 r1 三从板普通 RUN 峰值为
  914.572/905.044/903.912 µs；新 r2 NO2 的 964.792 µs 属于 STOP 过渡
  `256→0`，不与普通 RUN 相比，NO3/NO4 普通 RUN 峰值为
  915.020/905.624 µs。这些是各轮完整相位峰值，FEED 子段不是独立 FEED 最大值。
  保留快路径的依据是等价算术简化、代码缩小、无新增持久 RAM 和功能验收通过；
  不能据此关闭 `VDC-SCHED-001` 或宣称稳定超限率已下降。
- 原件复核：P3 的四份及前后比较的十六份 native binary 均通过 CRC/身份/
  epoch/JSON 比对，记录完整且 missed/reason 为零；稳定时间线逐条对应 native
  样本。新两轮三从板观察器均 ACTIVE、reason/fault 为零，joined 与 published
  同步递增，RX/TX elapsed 递增。最终四板 STOP、config ACK、SELFtest 全零且
  recorder SAVED；SAVE 只证明状态/epoch/长度，不声称 SD 与 SRAM 字节比对。
  证据根目录 `out/HardwareAcceptance/20260915/tdma-event-lift-r1/`；两轮比较
  原件分别在 `tdma-event-lift-after-r1/`、`tdma-event-lift-after-r2/`。
- 下一 gate：分解 `event_start_cut` 的身份/几何复验、MMIO、时间读取和发布开销，
  结合 Flash 放置评估收益；保留已退休后继续累计故障原因及 observer guard。
  当前仍为 850 µs TDMA / 1.5 ms 整表预算快照，本切片未修改预算，不表示达到
  500 µs 或整表 WCET。随后按既有依赖补共同 session、命令交接/应用与实际锁相。
  示波器继续用 NO1/CH1 上升沿触发、四路 DPLL 输出及 1× 探头；本轮未新增
  波形或锁相结论，未变更契约登记或 HAOFV owner 边界。

### VDC-PROGRESS-20260915-037 — transport CRC 等价优化与同槽四板对照

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  036 已提交为 `385a9ff` / `cb3cff5`，本轮只优化
  `tdma_transport_crc32_update()`：用反射多项式的半字节表替换逐位循环，保持
  任意初值、增量结果、取反约定，以及全部 identity/transport 校验调用、字节
  范围和拒绝顺序。不可变表与单一内核放 SRAM，不借用 DMA 或增加跨核可变状态。
  命令运输、PIO、调度预算、配置和 OTA 实现均未改变。
- 软件：实际生产内核对全部单字节、初值各位、空输入、非对齐、不同长度及分块
  连续计算进行多项式/zlib 差分；29 项 Python 回归通过，完整 transport 与
  adapter C suite 通过。release 双槽构建和 Flash 链接检查通过。
- 资源快照，非容量契约：初次 `noinline` 构建仍生成常量长度克隆，内核占
  356 B，BSS 对齐导致主 RAM 末端增加 4096 B；该构建未部署，日志保留。
  增加 `noclone` 后最终内核为 56 B、SCRATCH_Y 常量表为 64 B；内核利用已有
  BSS 对齐间隙，主 RAM 末端与 036 相同。SCRATCH_Y 到启动栈底剩 536 B，
  BSS 末端到 HeapLimit 的范围为 18740 B，不等同 RTOS 实时空闲堆。
- 当前源码四板 P3：`tdma-crc-r1/p3/` 耗时 177.459 s，
  `passed/flow_completed/strict_gates_passed=true`、诊断失败为空；四板 OTA、
  校准与普通 process-image 闭环通过。源码 SHA 为
  `91c29728498960a5841b3fa9c07b24bd25f8f36e85c49a613ae5fdb3db53b949`，
  包 SHA 为 `7901b4020c2ab08e7963ff8f9bc2b3d6395551b8f0dca35fb0f7e99cd79b936c`。
  双槽 ELF/map/反汇编及包已固化，不能按复用的 build ID 区分代码。
- 对照控制：保留 036 原始 profile，并在改代码前补测一次；为了让新 P3 部署后
  回到同一应用槽，先用既有 OTA 重装一次冻结的原固件到另一槽，四板通过，耗时
  104.031 s（本轮比较准备，不计入新 P3 耗时）。新源码 P3 使用本轮校准矩阵，
  其后的比较采集显式恢复固定的旧 matrix、row35 和板序；前后槽位读回均为
  NO1/NO2/NO3 槽 2、NO4 槽 1。两轮新采集仍采用 START 后 RESET、SRAM 记录、
  全部 STOP 后导出和 SAVE，每轮约 16 s，四板闭环均通过。
- 下表为同配置、同槽位下主板短窗测量快照，非 WCET 上界。完整峰值均来自
  普通 CYCLE_BOUNDARY 到 RUNNING，选中序号非 RESET 首相位：

  本次板端快照中 TDMA 预算为 850 µs、整表周期为 1.5 ms，取自静态调度快照；
  本切片未修改 `app_realtime_profile.c`，不表示已经达到先前的 500 µs 目标。

  | 轮次 | 完整 OTHER 峰值 µs | 稳定采样段 TDMA overrun/run | deadline 增量 |
  |---|---:|---:|---:|
  | 036 原固件 | 1014.576 | 139/832 | 101 |
  | 本轮原固件补测 | 1014.480 | 100/832 | 92 |
  | CRC 优化 r1 | 775.940 | 0/667 | 0 |
  | CRC 优化 r2 | 787.224 | 0/835 | 0 |

- 原两轮主板合计 239/1664 次超限，新两轮为 0/1502，支持保留本切片。
  新 r1 更早通过 startup，稳定采样段短于其余轮次，因此保留实际分母，未当作
  相同长度采样；整个 profile 和稳定采样段也是不同窗口。所选完整峰值中的
  TX 子阶段不构成所有 TX 尝试的最大值，不能据其差值认定 CRC 独占多少时间。
- 从板仍有超限：新两轮稳定段合计 NO2 为 18/1503、NO3 为 23/1504、NO4 为
  18/1505。新 r1 的 NO2/NO3 OTHER 峰值属于 STOP 过渡，分别保留为
  1007.176/961.396 µs，不与旧普通运行峰值拼接比较。未关闭身份检查或观察器，
  未宣称整张静态表、长期 WCET、所有从板预算或原先较短预算达成。
- 证据：`out/HardwareAcceptance/20260915/tdma-crc-r1/` 保存源码范围、构建发现、
  P3、固化包、profile 分解和独立审核；`tdma-crc-before-r1/`、
  `tdma-crc-after-r1/`、`tdma-crc-after-r2/` 保存固定配置原件，与 036 的
  `tdma-detail-profile-r1/` 对照。native binary 的 CRC/身份/完整记录与归档
  JSON 一致，各轮 SAVE 确认 SAVED；最终四板 STOP/config ACK、selftest 全零，
  无新电气关闭采集或 SD 字节对比。未新增契约登记或提升 DPLL lock。
- 下一 gate：主板短窗收益已复现，继续 `VDC-SCHED-001` 的从板事件 FEED 热点。
  优先评估无额外持久 RAM 的计数回绕 lift 等价快路径，保持多回绕歧义、边界、
  错误原因及整批故障退休；独立软件/P3 后再比较。只读模型和 SRAM 放置评估在
  `tdma-opt-candidates-r1/event-placement.json`，不代表候选已实现或有硬件收益。
  完成时序与时间输入后继续共同 session、命令接收/应用和实际四板锁相；示波器
  沿用 NO1/CH1 触发配置，当前未用本轮 TDMA 数据替代输出锁相证明。

### VDC-PROGRESS-20260915-036 — 普通发帧与从板事件服务耗时归因

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  本切片只扩展 `TDMA_SERVICE_TIMING_VERSION` 的计时记录和离线解析，不改变命令
  启用、收发算法、PIO、预算或 OTA。事件观察继续由 Core1 物理 owner 有界服务；
  Core0 负责原生记录，SCPI 仅触发，全部 STOP 后导出及顺序 SAVE。
- 先用 035 的当前固件和固定校准配置补完整 profile：r1 在 START 前 RESET，主板
  OTHER 峰值包含 ARMED 到 RUNNING；r2 在四板 START 后等待再 RESET，仍测得
  CYCLE_BOUNDARY 到 RUNNING 的高耗时。两轮四板闭环通过，说明问题不只出现在
  首次启动。RESET 由 Core1 在下一相位消费；两轮选中的峰值均非首个 RESET 相位，
  各自 reset generation 一致、invalid 为零，未把空的自主 RUN 组当普通运行数据。
- 新探针：事件服务分 ENTRY、HARVEST、CONVERT、FEED、FINAL_CHECK、START_CUT、
  RETAIN、PUBLISH；FEED 包含读取后的故障复验和诊断字段复制，FINAL_CHECK 包含
  最终故障触发的二次 feed。ENTRY 包含等待状态下的启动尝试，START_CUT 只计随后
  的 monitor；RETAIN 包含退休和旧 `service_max_us` 更新，最后快照复制另计 PUBLISH。
  普通 reference 发帧四条入口累计到 REFERENCE_TX，物理 callback 计为其子段
  REFERENCE_SUBMIT。父子不重复相加，完整峰值不跨 sequence 拼接。
- 软件与资源：相关测试共 30 项通过，包含真实 owner 路径、永久故障/最终复验、
  start-cut 硬件模型、普通发帧及异步 pending 的计时归属、旧版本解析。
  首次测试因证据目录父路径缺失失败；随后发现旧 owner 测试夹具缺少已有
  start-cut monitor 替身，补齐调用检查后通过；失败日志均保留。
  release 双应用槽构建及 Flash 链接门禁通过。目标 map 中计时静态区增加
  320 B，SCPI 局部 snapshot 增加 256 B（本轮构建快照，非 RAM 容量契约）。
- 当前源码四板 P3：`tdma-detail-r1/p3/` 耗时 203.115 s，
  `passed/flow_completed/strict_gates_passed=true`，诊断失败为空；四板 OTA 和
  普通 process-image 闭环通过。源码 SHA 为
  `b248a154ebc5286fea255b3c8e2a489cf10a948c320f7616502797e6995b07ee`，
  包 SHA 为 `9fdcba5c400de65294152b7e9f3ea0c7b18b7051e2b543a8675aa787f47f4764`。
  固化双槽 ELF/map/反汇编和包；复用的 build ID 不作为版本区分依据。
- 新分段复测仍用固定的旧校准配置、START 后 RESET，四板约 16.2 s 完成闭环。
  下表为同条 OTHER 峰值的测量快照，非 WCET 上界或产品事实源，单位 µs：

  | 板 | 完整相位 | 物理服务 | 时间换算 | 事件 FEED | START_CUT |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 1014.576 | 6.456 | 0 | 0 | 0 |
  | NO2 | 926.684 | 237.116 | 15.348 | 57.104 | 52.140 |
  | NO3 | 967.636 | 373.192 | 16.536 | 127.568 | 54.108 |
  | NO4 | 978.928 | 381.096 | 26.964 | 140.212 | 59.492 |

- 主板该相位为 CYCLE_BOUNDARY 到 RUNNING，REFERENCE_TX 占 651.768 µs，其中
  SUBMIT 占 105.556 µs，其余 546.212 µs 包含构造、校验和提交后的记账，尚不能
  全部称为 CRC 或复制。三从原生窗口的首条样本尚未启用观察器，随后样本 ACTIVE、
  published 持续增长且无 fault；换算只占事件服务一部分，不能按四次除法推算
  回收数百微秒。
  算术等价快路径的只读分析可以作为候选，尚未实施或声称硬件收益。
- 证据与复核：均位于 `out/HardwareAcceptance/20260915/` 下，旧固件 profile 在
  `tdma-phase-profile-r1/`、`tdma-phase-profile-r2/`，新固件在
  `tdma-detail-profile-r1/`，构建/P3/分析和独立审查在 `tdma-detail-r1/`。
  主控重解 P3 四份及 profile 十二份 native binary，CRC、身份、完整记录及
  归档 JSON 一致；profile 原始响应重新解析、嵌套余量非负。各轮 SAVE 确认 SAVED，
  不扩称为 SD 字节对比。最终四板 STOP，诊断输出关闭。
- 限制与下一 gate：profile 在 STOP 后仍更新，覆盖 RUN、STOP/idle 和导出服务，
  不等同隔离的稳定窗口 WCET；新增探针、快照复制及 OTA 槽位改变影响时间，不能
  将旧/新峰值差当算法收益。现有 P3 不约束 TDMA phase WCET，全表时序仍未闭合。
  `VDC-SCHED-001` 下一步优先定位普通发帧准备/校验/记账及从板 FEED/START_CUT，
  按收益选择一个等价或有界优化并独立 P3，保留身份与故障复验。共同 session、
  可信时间映射、命令实际应用和正式锁相继续未完成；示波器按 035 的 NO1/CH1
  触发配置用于后续同窗输出验证，本轮未新增波形或提升 lock 结论。

### VDC-PROGRESS-20260915-035 — 固定配置快速复测与四通道触发采集

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  前一切片已分离提交为 `e3d8737` / `4164b11`。本轮使用其已部署固件和既有
  工具进行测量，没有增加生产功能、改变预算或重新宣称命令启用态通过。
- 前后归因复核：033/034 两轮不仅源码不同，校准偏移行、部分有向链路延迟、
  实际应用槽位也不同；相同 build ID 不能替代源码/包 SHA。原 166/343 是含启动
  的记录 baseline 至末样本计数，稳定窗分别为 71/833 与 148/833 次 TDMA 超限。
  因此不根据这两轮直接归因或回退本地 STOP 取消修复。
- 快速流程：固定 034 的 matrix、偏移行和板序，直接运行既有
  `trn03_closed_loop.py`，SCPI 只触发，板端 SRAM 定时记录，全部 STOP 后导出。
  前两次复测因上轮记录仍 FROZEN 而拒绝 ARM，失败原件保留；按既有
  `diagnostics_tdma_record_save()` 保存至 SD、确认 SAVED 后再 ARM，恢复成功，
  未放松生命周期保护。随后每轮导出完再 SAVE，使下一轮可重复执行。
- 测量快照，非事实源：r3/r4/r5 分别耗时 15.125/14.984/15.031 s，闭环和既有
  实时 gate 均通过。各板每轮 14 个样本，记录器 missed/reason 均零；独立重解
  十二份 binary，终结 CRC、身份及 JSON 副本一致，STOP/config ACK 和 SAVED
  读回通过。本轮未做 SD 文件逐字节回读，不能把 SAVED 扩称为 SD/SRAM 一致。
  主板稳定窗 TDMA overrun/run 分别为 117/834、153/832、104/833，deadline
  增量为 85/112/76，超限率范围 12.48% 至 18.39%，合计 374/2499。
  034 的稳定窗比例落在本次范围内，只能说明当前固件仍有时序压力；稀疏
  last-runtime 不是逐周期分布，现有 gate 仍未覆盖 TDMA WCET。
  采样配置固定不等于完全相同启动历史，尚未进行受控旧/新固件 A/B。
- 外部观测：用户确认 RIGOL HDO4404 的 CH1 至 CH4 对应 NO1 至 NO4 DPLL 输出，
  探头均为 1×，以 NO1/CH1 正沿为触发源。本轮实际确认 SING 后 WAIT、armed
  时 SING/CH1 读回、随后自动 STOP，在同次 STOP 中导出四路 RAW BYTE。
  每路完整 block、preamble、请求/读回范围、SHA 和触发状态序列均保留。
  四路共同时间轴为 20 ns 采样、每路一百万点，窗口约 20 ms；上升沿数量为
  3/3/4/3，均有真实脉冲（本轮诊断快照，非精度或稳定性事实源）。
- 采集失败与修正记录：r1 在 SING 后立即读到旧 STOP，后续复验发现 WAIT，
  未导出；r2 观察到 WAIT 但等待内无触发，也未导出。r3 完成新触发后，仪器
  sweep 自动读回 NORM，保守检查拒绝；原件保留，随后只补读其冻结帧，未冒称
  新采集。该帧只有 NO1 输出，诊断启动顺序调整为从板先于 NO1 后，r4 四路均
  有脉冲。r3 输出关闭的短 ACK 等待超时，后续原始查询确认四板已关闭；r4
  控制响应及最终 selftest 全零、TDMA STOP/config ACK 均闭合。
- 范围：本次输出是 TDMA STOP 下既有 phase-only 诊断 persona，不是正常
  resident 命令驱动的产品输出。其 DCO 过旧时可回退本地 monotonic deadline，
  每次 service 只排一个脉冲，不能按配置周期补出未采到的脉冲、强行按同周期
  配对或宣称连续锁相。最终关闭是软件读回，未补测关闭后的电气波形。既有
  `scope_dpll_capture.py` 的默认滚动采集、缩放和原始数据保留仍需独立工具
  切片修正；本轮 `out/` 中诊断采集不代表该工具已验收。
- 证据：`out/HardwareAcceptance/20260915/command-stop-timing-baseline-r1/`
  至 `command-stop-timing-baseline-r5/`、`command-stop-timing-save/` 保存原生记录、
  失败、SAVE 读回和 `timing-review.json`；`scope-trigger-r1/` 至
  `scope-trigger-r4/` 保存各次原件、板控和波形。示波器后续用于相对 NO1 的
  相位、漂移、缺脉冲和恢复观测，需与同窗板端命令应用、角色、会话和可信时间
  映射对账；探头/通道延迟及最终门限仍待验证，不能单靠内部 LOCKED 或单帧判断。
- 下一 gate：`VDC-SCHED-001` 先补同 reset generation 的完整 TDMA phase profile，
  区分 outer、adapter、RX、overlay 和 owner 成本，嵌套阶段不重复相加；按校准
  配置与应用槽位受控后再做代码 A/B。命令会话、定时应用及正式输出锁相任务
  继续保持未完成，不因测量链路可用而提升状态。

### VDC-PROGRESS-20260915-034 — 本地 ring 配置绑定与 STOP/ARM 命令取消

- TODO task ID：`VDC-CMD-002`、`VDC-ROLE-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  前一切片已分离提交为 `66919c2` / `91adaaa`，随后开始本切片；本切片代码及 P3
  凭证已提交为 `e3d8737`，文档另行提交。
- 反例：普通 TDMA STOP/ARM 更新 ring config sequence，却不改变 VDC epoch/run
  或本地角色代际。冻结旧源码的真实 service STOP/ARM、RefMem receiver/getter、
  manager 函数与完整 Domain 执行后，STOP 前已收且未来未到的命令在重新 ARM 后
  写入 DCO；Core0 看见或漏看中间 STOP 均复现。无 STOP 正测与已应用命令不重放
  对照通过。adapter 与硬件时钟为 host stub，不能作为已发生的实板故障归因。
  旧紧凑 clock getter 还在 config/result 之间缺少末尾配置复验；真实 runtime
  configure/service 插入该窗口后，getter 返回旧 config，现场 config 已变化。
- 修复：RefMem 把 local role 与 `consumer_ring_config_seq` 一起放在命令 guard
  内绑定；变化时作废 retained 值，保留同 wire 会话的来源排序水位。Core0 只在
  enabled/started、config/applied ACK 及 schedule/slot 一致时绑定非零配置；
  关闭态绑定零并取消待准备 resident 记录和组装、推进 FIFO admission epoch。
  快照暂忙不取消原值，下次 RefMem 服务再试；普通 mailbox 仍继续。
  `refmem_sync_vdc_receive_admitted_frame()` 覆盖 resident、node-load COMMAND
  和遗留 follower completed 入口，关闭期间不能通过兼容路径写入大序号污染水位。
  Core1 先读取当前配置，再 guarded copy，并在来源/目标处理及消费任何序号前
  复验 config/applied；STOP/config 在复制中到来时退出，不调用应用端或消耗序号。
  紧凑 clock getter 在同一次有界尝试内复验 config/result 两个 guard，并携带 ACK；
  时间映射与 ring observer 拒绝未 ACK 配置。Core1 没有新增锁或等待。
- 软件与构建：主控相关 Python 合跑 84 passed，文档检查器回归 18 passed
  （本轮快照，非事实源）；Domain、RefMem、ring runtime 和 time mapping C
  runners 均通过。新增 STOP 场景执行真实完整 Domain，包含漏看 STOP、未 ACK、
  复制中途换配置/绑定、快照忙恢复、零代际及兼容入口大序号拒绝；关闭期普通
  DELTA 仍更新，重新 ARM 后新命令可应用。clock getter 负测执行真实
  configure/service 插入原子读取窗口并验证重试有界。默认双应用/Boot 构建及
  命令开启分支 compile-only 通过；独立只读审查无阻断项，逐文件哈希核对一致。
- 资源快照，非事实源：ARM receiver/retained command/compact clock snapshot
  分别保持 456 B/72 B/152 B，新字段使用既有对齐空间；默认双应用 BSS 起点各移
  后 8 B，BSS 终点、HeapLimit/StackLimit 均未变化，RAM 高水位未增加。
- 硬件验收：源码指纹
  `a7ddeb069eecf6968ecb4c89ce80e22a2768037805f36961b9de7b014f62626d`
  的当前四板 P3 按既有 quick diagnostic profile 通过，凭证
  `strict_gates_passed=true`，闭环/现有实时门禁通过、diagnostic failures 为空。
  耗时 190.348 s，各板原生记录 14 条、记录器 missed=0（本轮快照，非事实源），身份、
  完整性和 20 项产物哈希核对通过；导出前全部 STOP，config/applied ACK 一致，
  FIFO_RESET 均 OK 且回收读回正常。包与双应用 map 已归档。
- 时序复核范围：`schedule-comparison.json` 保留相邻两轮原生采样窗口的逐板对照。
  本轮 VDC/DPLL 相位的 overrun/deadline 增量均为零，但 TDMA 相位仍有超预算：
  主板本轮 overrun 增量为 343，上轮为 166；采样到的 last runtime 最大值分别为
  897.536 us 与 834.508 us（窗口快照，非 WCET 事实源）。当前
  `trn03_closed_loop.py::validate_dpll_schedule()` 不以 TDMA phase WCET 作门禁，
  因此 P3 通过不关闭静态调度预算；本轮未做同工况 A/B，不能将该差异归因到此次
  配置读取。该时序风险继续由 `VDC-SCHED-001` 跟踪，不修改验收工具或预算掩盖它。
- 证据：`out/HardwareAcceptance/20260915/command-stop-r1/`；`before-stop/`
  保存旧源码、真实完整 Domain 反例及正对照，`before-clock/` 保存旧快照交错反例。
  `after-stop-r3-result.json`、`software-verification.json`、`source-review.json`、
  `source/`、`build-result.json` 和 `target-layout.json` 保存测试、冻结源码与资源。
  `p3/acceptance.json`、`p3/diagnostic.json`、`p3/tdma-process-image/`、
  `review-final.json` 保存硬件原件及主控核验。
- 范围与下一 gate：本切片只闭合本地 ring 生命周期的命令准入，不建立四板共同
  session。config sequence 为零时只关闭命令准入，后续非零 ARM 可恢复，普通
  TDMA 回绕行为不变。最终 Core1 检查之后到来的 STOP 由后继 owner 边界完成，
  不承诺 SCPI 请求瞬间撤销。已进入 TX image/FIFO/PIO/DMA 的旧片段、上游旧输入
  和完整旧记录重发仍需 wire 有效期与共同 session 规则；命令默认禁用，实际定时
  应用和输出锁相未验收，`VDC-CMD-002` 仍 PENDING。继续增加功能前，先对本轮
  TDMA 超预算增多做同工况对照，保留校准/板卡/负载配置及原始窗口，避免把随机
  工况差异或 gate 未覆盖项当作已定位的回归。
- 实测设备准备：用户接入
  `USB0::0x1AB1::0x0610::HDO4A244301137::INSTR`，本轮只读 VISA 查询确认
  RIGOL HDO4404、序列号匹配，原始设置保存在
  `out/HardwareAcceptance/20260915/scope-connect-r1/instrument-state.json`。
  用户确认 CH1 至 CH4 分别连接 NO1 至 NO4 的 DPLL 输出，实物探头均为 1×；
  该连接记录不包含实际输出相位测量。

### VDC-PROGRESS-20260915-033 — Core0 FIFO 借用保护与 STOP 回收互斥

- TODO task ID：`VDC-CMD-002`、`VDC-ROLE-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  032 已分离提交为 `3a42dcc` / `34aa9fc`，随后开始本切片；本切片代码与 P3
  凭证已提交为 `66919c2`，文档另行提交。
- 反例：旧 service 仅核 enabled/adapter/engine 状态后直接清 FIFO；已 acquire 的
  RX view 仍持旧 slot 指针，后续 Core1 发布可覆盖它，旧 release 又能释放新 slot。
  真实旧 service/FIFO harness 输出 `reset=1, old_pointer_overwritten=1,
  old_release=1, acquired_new=0, drops=1` 后断言失败（本轮快照，非事实源）。
  这是 API 允许交错的反例；当前 RefMem 任务优先级高于 SCPI，不能把低优先级
  SCPI 抢占 RefMem 当作已测实板故障根因。SCPI raw TX/RX 与 RefMem 共用 FIFO
  也需要任务间互斥，因此单纯把 reset 移至 RefMem 末尾不足以关闭所有借用窗口。
- 修复：`core0_guard` 串行化 Core0 FIFO publish/reset，成功 acquire 持有按 slot
  编码的 guard，release 先归还 slot 再解锁，避免归还因第二次 try-lock BUSY 丢失。
  Core1 不读、不取新 guard；可在旧 release 解锁前重新发布已 FREE 的 slot，旧
  release 此后不再写该 slot。错误 slot、未借用或重复 release 被拒绝。
  API 现在要求同一 FIFO 同时最多一个 Core0 RX view；两个实际接收 caller 均逐个
  acquire/parse/release，忙时保留数据等下次服务，不清理别人的 view。
  `tdma_service_reset_flight_fifo_checked()` 持 `ring_control_guard` 核物理 STOP、
  config/applied ACK、engine inactive 与停止态更新空闲，防 ARM/config 与回收交错。
  SCPI 仅对 BUSY 按 `SCPI_TDMA_FIFO_RESET_WAIT_LOOPS` 有界让出后重试，真正完成
  才返回既有 OK；NOT_STOPPED/INVALID 立即拒绝。未改 OTA 或命令使能。
- 软件与构建：主控 Python 合跑 64 passed（本轮快照，非事实源）；FIFO、service
  scheduler、adapter C runners 均通过。测试执行生产 FIFO memcpy 中途的 Core0
  竞争、owner FREE 后的 Core1 重发、借用期间拒绝、失败恢复、STOP ACK 未到及
  真实 service reset 边界中的 ARM/config 竞争；SCPI handler 执行真实函数，但
  reset 返回和 RTOS 调度为观测 stub，不冒充实板强制交错。
  默认双应用/Boot 构建、命令开启分支 compile-only 通过，独立只读审查无阻断项。
- 资源快照，非事实源：ARM FIFO 从 1888 B 增至 1896 B，RX slot/view 保持
  296 B/40 B；默认双应用 BSS 各增加 8 B，HeapLimit/StackLimit 不变。
- 硬件验收：当前源码指纹
  `db0ae540cf417d02801fe57d712bb2056f81abb1f718ed86cb01a4710d2a2be1`
  的四板 P3 严格通过，闭环、实时门禁与诊断均通过，无强制放行；耗时
  188.407 s（本轮快照，非事实源）。四板 `FIFO_RESET` 均返回 `OK`，回收后
  队列/活动 TX 读回通过；各板原生记录 14 条、missed=0，身份与完整性核对通过。
  导出前四板均 STOP，config/applied generation ACK 一致。主控复核凭证与源码
  一致及 20 项产物哈希，并归档包与双应用 map；本轮仅验收命令禁用态四板基线，
  不作为实板强制交错、命令定时应用或实际输出锁相的证明。
- 证据：`out/HardwareAcceptance/20260915/fifo-core0-reset-r1/`；`before-reset/`
  保存冻结输入与旧版反例；`after-service-result.json`、`pytest-final-result.json`、
  `pytest-final-completion.json`、各 C runner result、`build-result.json`、
  `target-layout.json`、`source-review.json`、`source/` 及 `independent-review.json`
  保存软件、目标资源和冻结指纹；`p3/acceptance.json`、`p3/diagnostic.json`、
  `p3/tdma-process-image/` 与 `review-final.json` 保存严格验收与主控原件复核。
- 范围与下一 gate：本切片保护 FIFO 借用与回收，不关闭完整命令 STOP/session
  取消。`reset_stopped()` 仍保留 admission epoch；Core1 正确停机仍由 service
  与调用方提供，底层 FIFO 不操作 PIO/DMA。上游旧输入、完整旧记录重发、共同
  session、远端重启、payload 版本及交付上界仍需闭合；命令默认禁用，实际输出
  锁相未验收。

### VDC-PROGRESS-20260915-032 — RX FIFO 发布代际与旧命令入站取消

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-ROLE-005`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。031 已分离提交为 `e333da5` / `85cde62`，随后开始本切片。
  本切片代码与当前 P3 凭证已提交为 `3a42dcc`，文档单独提交。
- 反例：实际 FIFO 中旧命令的起始片在角色 A→B→A 后才被解析，随后同一记录的后续
  片段使旧未来命令获得新角色绑定；Core0 漏过中间 B 时也会发生。以下为反例快照，
  非事实源：两次旧版执行均接收 command sequence 41，ordinary mailbox 更新
  15 次，最终拒绝断言失败；同代对照通过。完整 payload 分片数超过 FIFO 容量，
  测试按真实容量排队起始片再送后续片，不伪造同 source 多邮箱。
- 修复：TDMA FIFO 新增独立 `rx_admission_epoch`，只由 Core0 RefMem 接收任务
  在线推进；Core1 在 RX payload 复制前捕获，slot/view 原样携带。Core0 身份或
  角色刷新先取得新 grant，再 reset/bind/cache；同身份不推进，owner 不可用或
  代际耗尽时返回无效 grant 并保持拒绝，禁止回绕复用。接收只过滤旧代际 command
  mailbox，保留同 view 普通 RefMem 更新与正常 release。使用已有片段拒绝计数，
  不增加实时解析、wire 字段、PIO/DMA 操作或 OTA 改动。
- 软件与构建：真实 FIFO、RefMem、TDMA service scheduler 和 adapter C runners
  通过；主控 Python 合跑 41 passed（本轮快照，非事实源）。完整 ingress 测试执行
  实际 refresh、receive、mailbox parser、分片、frame CRC 与接收保留，owner 查询
  为 stub；覆盖已排队片段、复制中途实际刷新、同代半组装保留、身份变化、grant
  失败重试及新完整命令恢复。独立 FIFO 测试另覆盖 acquired view、head/transport
  回绕、合法 STOP reset 和耗尽后普通数据连续。独立只读审查无阻断项。
  默认双应用/Boot 构建及命令开启分支 compile-only 通过，开启分支未部署。
- 资源快照，非事实源：目标 ARM ABI 的 FIFO、RX slot、RX view 分别保持
  1888 B、296 B、40 B；新增字段使用已有 padding，默认双应用 BSS、HeapLimit、
  StackLimit 均未改变。原件见 `target-layout.json` 与目标 map。
- 硬件验收：当前源码指纹
  `b540c4bd4ac225faa0aed0ed55b117d7f79292407baa7fa749b141f44b248124` 的独立
  `p3_hardware_acceptance.py run --tdma-only` 严格通过；acceptance/diagnostic 的
  strict gates 均通过且失败列表为空，TDMA closed-loop/realtime 通过。
  以下为本轮快照，非事实源：耗时 181.704 s，四板各 14 条原生记录，missed=0；
  四板全部 STOP，配置代际 ACK 一致。`review-final.json` 核对当前源码与凭证指纹、
  20 项原件散列、板端记录身份及 STOP 交接；包和双应用 map 已封存。
  本轮验收的是默认禁用命令的四板基线，不能提升为命令启用态或硬件 DCO 验收。
- 证据：`out/HardwareAcceptance/20260915/command-ingress-epoch-r1/`；
  `before-ingress/` 保存旧源码 SHA、原始 harness/exe、断言失败和同代对照；
  `after-ingress-result.json`、`ingress-final-result.json`、各 C runner result、
  `build-result.json`、`source-review.json`、`source/` 及 `independent-review.json`
  保存软件、构建与主控冻结证据。最终测试 fixture 要求新增 admission API，旧版
  反例须使用冻结的旧 harness，不能混入新 fixture 后声称重跑旧源码。
- 范围与下一 gate：取消的是 producer 已捕获旧 tag 的 FIFO 发布，含排队和复制
  中途；边界是 Core0 刷新，不是精确的 Core1 角色激活时刻。DMA/station 尚未进入
  发布的旧输入、之后完整重发的旧记录仍须协议有效期与切换规则。
  `reset_stopped()` 保留 admission epoch，仅回收队列；它要求无在用 view，
  SCPI reset 与 RefMem 借用的端到端协调仍未验收，不据此宣称完整 STOP 取消。
  命令保持默认禁用；共同 session、远端重启、版本兼容、实际交付上界、共同时间
  及主从定时应用仍需闭合，四板实际输出锁相未验收。

### VDC-PROGRESS-20260915-031 — 角色代际绑定与已接收命令防重放

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-ROLE-005`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。030 已分离提交为 `8f530ed` / `50588c0`，随后才开始本切片。
- 反例：旧实际 manager 函数体在 A→B→A 及 FOLLOWER→MASTER→FOLLOWER 后均重复
  提交旧 retained 命令。`out/HardwareAcceptance/20260915/command-role-fence-r1/`
  的 `before-owner/` 保存旧函数来源 SHA、可执行 harness、两次断言失败及原始输出。
  Domain 应用为观测 stub，因此它证明重复提交路径，不冒充物理 DCO 的波形反例。
- 修复：Core1 将当前实际 `control.profile.generation` 传给命令 getter，
  `refmem_sync_vdc_copy_command_for_generation()` 在同一 guard 内核对 context
  的本地 consumer generation 与完整副本。Core0 RefMem 任务在本地角色代际变化时
  作废所有 retained 值并取消当前片段组装，保留各来源 command/frame 序号水位；
  排序依据不再使用 `valid`。已见旧命令换新 transport sequence 也不能恢复有效。
  同代际调用无副作用；epoch/run 等接收身份 reset 才退休历史。parser 在组装前
  要求 transport/context 本地 generation 一致，避免两次快照跨代消耗新命令。
- HAOFV 与资源：角色身份由 Core1 拥有，接收状态仍由 Core0 RefMem 任务唯一写入；
  不比较本地 role generation 和远端 command generation，不改 wire 或 OTA。
  默认 release 双应用/Boot 和 command 开启分支 compile-only 通过，开启分支未部署。
  以下为目标 map 快照，非事实源：接收 context 从 448 B 增至 456 B，双应用 BSS
  均增加 8 B，Heap/Stack limit 不变；原件见 `target-layout.json` 和 link map。
- 软件结果：真实 receiver/binding/copy 的 C runner 通过；角色、待执行命令、Core0
  延迟/漏代际与复制交错均有正反验证。以下为本轮快照，非事实源：owner/ingress
  Python 合跑 30 passed，C 测试包含 4 次复制中途写入。新增 ingress harness
  执行实际 refresh 函数及 parser 准入前缀，使用真实 RefMem 库和 mailbox CRC，
  不覆盖后续全片交付。该测试曾暴露函数提取器误匹配更早的 `if (refresh(...))`；
  修正为行首有类型的定义后全绿，失败原件见 `ingress-extractor-before.json`。
  最终独立只读审查通过，仍明确 manager 的 Domain 应用端是观测 stub。
- 硬件结果：当前源码指纹
  `14f00d1d4131e4987addf7189bd7121454f33f8e34e0c33a62f292d4375bb759` 的独立
  `p3_hardware_acceptance.py run --tdma-only` 通过；`p3/acceptance.json` 与
  `diagnostic.json` 均为 strict gates passed，失败列表为空，短帧 closed-loop/
  realtime 通过。以下为本轮快照，非事实源：总计 198.460 s，每板 14 条原生记录，
  missed=0；四板全部 STOP，配置代际均已 ACK。`review-final.json` 核对当前源码
  与凭证指纹、20 项原件散列及板端记录身份，固件包及双应用 map 已另存本切片目录。
  本轮部署并验收的是命令默认禁用态，不提升为角色切换的实板命令功能验收。
- 范围与下一 gate：本切片只关闭已接收/已见命令在角色回切后的重放，不关闭完整
  角色切换协议。切换前已准备或排队、切换后才首次完整收到且序号更大的未来命令，
  仍可能满足本地新 tag；必须继续建立 TDMA ingress fence 及切换生效定义，不能
  靠清组装、丢首帧或随意增加本地 run 代替。远端 generation 重启、共同 session、
  Domain clock/oscillator 请求历史、版本兼容和交付上界仍是命令启用前置。
  命令运输保持默认禁用，不据此宣布三从应用或正式锁相完成。

### VDC-PROGRESS-20260915-030 — 命令区唯一写者与在线 reset 一致性

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  普通四板基线已分离提交为 `d01c824`（代码）和 `9f62d1e`（文档）；本切片继续
  保持 `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认关闭。
- 原因与收益：原接收区可由 Core1 及 Core0 的 SCPI/RefMem 不同任务触达，在线
  调用冷 `init` 还会归零 guard。真实 reader copy 中途 reset 再发布的反例接受了
  旧 command sequence 与新 phase 的混合副本。这是可复现的数据一致性缺陷，
  有修复价值，不通过隔离命令运输而将其永久搁置。
- 修复：仅 Core0 RefMem 任务刷新/退休命令区，service 入口先取得可信身份再
  接收；Core1 和 SCPI 不再直接清空。snapshot 失败只暂停命令准入，普通 flight
  继续。在线 `refmem_sync_vdc_reset()` 在原 guard 内清空并更新身份，保留发布
  序列；忙标记后增加写屏障。Core1 在序号消费前拒绝旧 epoch/run，并在会话变化
  时重置本地序号水位；未更改 wire、OTA 或 DCO 使能。
- 软件证据：`out/HardwareAcceptance/20260915/command-lifecycle-r1/` 保存旧源文件、
  当前源码副本、diff、编译命令、失败/成功原件和独立审查。以下为本轮快照，非事实源：
  旧实现反例产生 `seq=11, phase=-22` 后断言失败，修复后交错/身份/回绕测试通过；
  host runner 38/38 通过，生产 manager/service 函数体测试 20 passed。首次 host
  运行曾因临时 CRC 链接缺失 `portable_ota_port.h` 失败；测试改用既有 CRC fixture
  后完整重跑通过，旧失败另存 `host-unit-r1-failure.log`。
- 构建与边界：默认 release 双应用/Boot 构建及 flash link 通过，启用 command
  的 RefMem 分支另做 ARM compile-only 验证且未部署；最终独立只读审查未发现本
  切片阻断项。双应用 link map 与 `target-symbols.json` 保留新增会话水位及 reset/
  copy 的实际地址和符号尺寸，不从单个符号推算总 RAM 变化。Python 测试使用真实
  时间映射，但 Domain actuator 是观测 stub，
  只证明 manager 的准入及应用尝试，不能证明真实 DCO 或完整 Domain 换会话行为。
- 硬件结果：独立执行 `p3_hardware_acceptance.py run --tdma-only`，当前源码指纹为
  `43fe95f0777f4b97fa58f35f7391c8f8a4537874c2c06b7347dff2aca4ec28b6`，build
  `20260915022619`。`p3/acceptance.json` 与 `diagnostic.json` 的严格门禁均通过，
  失败列表为空；四板短帧 closed-loop/realtime 通过，四板全部 STOP 后交接原生记录。
  以下为本轮快照，非事实源：预算计时 194.015 s，每板 14 条样本且 missed=0。
  `review-final.json` 复核当前源码/凭证指纹、20 项原件散列、记录 build/身份和 STOP
  代际 ACK；本轮固件包另存切片目录。通过范围是默认命令禁用态的四板 P3。
- 下一 gate：`VDC-CMD-002/003` 继续保留同会话角色/来源 A→B→A、远端 generation
  重启、`vdc_domain_publish_clock_model()` 任意换会话后的 Domain history 退休、
  schedule 与完整 STOP 取消；payload 版本、共同 session 和交付上界仍是启用前置。
  不将本切片提升为命令启用态、三从应用或正式锁相完成。

### VDC-PROGRESS-20260915-029 — 粗校准 STOP 后配置拒绝的有界恢复

- TODO task ID：`VDC-TDMA-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。028 的三轮
  验收原件均已复核，短帧闭环通过但严格校准仍有失败；本切片仅处理已重复出现的
  TOPology 准备拒绝，不开放新的 VDC 命令功能。
- 诊断：两轮失败前已读回 `ring_enabled=0`、`ring_adapter_started=0`，且当前
  config generation 与 applied generation 相等。TOPology handler 失败推入
  execution error 而不返回结果 tuple，主机因等待结果显示 timeout。这不是固件
  执行耗时，也不是 quick action timeout 太短。STOP ACK 不能证明 Core0 控制锁、
  队列退休、intent completion 以及所有快照读同时可用；当前通用错误未区分具体
  拒绝点，不能声称已经定位某个 seqlock 或锁为实测根因。
- 修复：`calibration_clk_train.py::_set_stopped_topology()` 仅在无结果且 error
  queue 明确返回 execution error 时，按 `TOPOLOGY_ATTEMPT_LIMIT` 有界重发同一
  配置；每次先重新核验 STOP 与当前代际 ACK。错误 tuple、其他错误及 STOP 失效
  不放行。成功必须收到正确 tuple，再等待新的 config generation 与 applied
  generation 相等，全部板完成后才 ARM；ARM 后继续校验实际 topology。STOP 的
  runtime topology 会被 owner 清空，不用这些零字段校验 staged topology。
- 所有者边界：相同参数的重复配置可收敛到同一目标，但 setter 并非无副作用事务，
  成功会发布新 STOP/config generation，失败前也可能已更新部分配置。因此保留
  每次拒绝原件、重新确认停止并等待新代际，不复用配置前 ACK，不在固件增加等待。
- 软件反例：已有 `arm_training_persona()` 在临时拒绝后中止的反例先失败；修复后
  覆盖重试成功、固定上界、错误 payload/错误码、STOP 丢失、旧代际及新代际未 ACK
  的正反验证通过。与 coarse/coded/topology/TRN-03 相关 Python 回归共 159 passed
  （本轮快照，非事实源）；双应用/Boot 目标构建及 flash link 检查通过。
- 证据：`out/HardwareAcceptance/20260915/coarse-topology-recovery-r1/` 保存修复前
  工具/测试副本、当前源文件、diff 和 `software-verification.json`。当前源码指纹为
  `a05085436c36d50903646f27cfb3e490f3fbf1b7719adf055514993b6bf9dc03`；随后独立
  执行正式 `p3_hardware_acceptance.py run --tdma-only`，包含当前构建与四板部署。
- 硬件结果：该目录 `p3/acceptance.json` 与 `diagnostic.json` 的
  `passed/flow_completed/strict_gates_passed` 均为真，失败列表为空；coarse CLK、
  SCK training、replay matrix 和 TDMA closed-loop/realtime 均通过。四板全部
  STOP，原生记录交接通过。以下为本轮快照，非事实源：总预算计时 184.344 s，
  每板 14 条记录，四种 reference 配置共 16 次 TOPology_APPLIED 均复核新代际
  ACK。本轮没有实板重试，重试分支的恢复和拒绝边界由 host 反例覆盖，不能声称
  已在硬件上复现并消除某个具体锁冲突。
- 主控复核：`review-final.json` 核对当前源码指纹、当前凭证、20 项引用原件散列、
  每次 topology 前后代际及四板 STOP 记录；固件包另存切片目录。此前三轮失败均
  保留，后来的通过不追认旧结果。当前完成普通四板 quick 诊断基线收敛，不提升为
  命令启用态或 DPLL 锁相验收。
- 独立只读复核：`command_gate_audit` 确认拒绝不提升为成功、新代际 ACK 和全板
  ARM 屏障成立。底层具体暂态拒绝点及 SCK 候选稳定性仍需单独诊断，不由有限恢复
  或一次通过追认历史失败。下一 gate 保持 `VDC-VERIFY-001` 与既有命令前置依赖。

### VDC-PROGRESS-20260915-028 — resident 命令编译隔离与四板收敛

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。前序 027 完成独立 P3 后才开始本切片。
- 变更：`DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认关闭，使用条件
  编译隔离 resident 命令 prepare、TX 编码/游标、RX fragment 分派、identity 同步
  及私有 record/组装状态。普通 compact mailbox 继续处理，command class 在普通
  RX 中被拒绝。诊断 snapshot 字段保持存在且清零；通用命令校验、共同时间准入和
  follower 时间门禁保留。旧 standalone 排他条件仍在，不能称为旧 VDC 功能恢复。
- 软件与资源：`pico2-release` 双应用及 Boot 构建通过；host runner 38/38 通过；
  文档检查器和 DPLL capture Python 回归 36 passed。使用真实目标编译命令分别
  覆盖开关关闭/开启，均可编译；预处理原件确认 prepare/parser/identity helper 和
  私有 record/fragment 状态只存在于开启分支。开启分支仅做编译验证，没有部署。
  以下为目标对象快照，非事实源：`s_tdma_flight_sync` 从 1280 B 降至 1104 B，
  释放 176 B。数据及命令见 `compile-isolation.json`，不把局部 RAM 回收当成 WCET
  或全表调度收敛证据。
- 当前源码指纹为
  `742074468a80d50a6bfdb8a0c38e1daf15e3da6f3ce978873f26ddeeacf9a652`。
  增量构建沿用 build `20260915022619`，必须同时核对 source/package SHA-256；
  build ID 相同不代表两切片固件相同。完整证据根为
  `out/HardwareAcceptance/20260915/command-isolation-r1/`。
- 首轮 `p3/` 已完成正式 `run --tdma-only`，四板部署和短帧 closed-loop/realtime
  通过、全部 STOP；但 `strict_gates_passed=false`，保留 SCK training 和 replay
  row selection 两项失败。以下为本轮原件快照，非事实源：17 次 SCK 测量全部有效，
  NO2→NO3 的 8 次结果只有一个偏移候选，不满足候选覆盖；四组候选均未满足重臂
  预算，最小从板余量为 -1 sample。`[1,0,1,0]` 是偏移值，不是缺样数量。
  `p3-failure-review.json` 保存分析；基础流通过不抵消校准失败。
- 恢复范围：源码及固件包 SHA-256 与首轮部署一致，随后使用受支持的 `resume
  --tdma-only` 入口，仅复用该轮成功 OTA 原件，重新核验 live build、软件复位、
  校准及 TDMA。恢复原件单独写入 `p3-resume-r2/`，不覆盖首轮失败。
- 第二轮 `p3-resume-r2/` 的 SCK training 与 replay matrix 均通过，短帧闭环、
  实时检查及 STOP 后记录交接通过；但粗 CLK 校准准备阶段 NO4 的
  `SYSTem:TDMA:RING:TOPology 4,3,0` 返回 timeout，伴随 SCPI execution error，
  `strict_gates_passed` 仍为假。它是本轮原始控制流程失败，不能推定为前一轮
  SCK 候选不足的同一原因，也不能据此归因到 resident 隔离代码。
- 第三轮 `p3-resume-r3/` 在 NO2 再次出现同一 TOPology 准备拒绝，其余校准及
  短帧通过；三轮 `strict_gates_passed` 均为假。四板每轮均 STOP，原生记录交接
  通过；`review-final.json` 复核三轮源码、包及引用原件 SHA-256。以下为本轮
  `acceptance.budget` 快照，非事实源：首轮含 build/OTA 为 185.410 s，两轮同源码
  恢复为 75.078 s、71.510 s；不把嵌套 timing duration 累加为总耗时。停止盲目
  复测后，准备流程修复转入 029；本条不能单独声称严格 P3 已通过。
- 独立只读复核：`command_gate_audit` 检查开关两态、私有字段引用、普通 builder
  和旧排他条件，未发现需追加的隔离修复。TDMA TODO 同时清理重复任务行和悬空
  引用，稳定 Task ID 保留，未提升任务或契约状态。
- 下一 gate：重新启用前须闭合 context reset 的唯一写者/guard、payload 版本
  兼容，以及发布间隔/FIFO 背压/重复片段下的交付上界。它们作为原型待办保留；
  每项修改单独做 host、目标构建、四板 P3 和对应功能验收。命令 apply、DCO 跟随、
  全表 WCET 及 formal lock 仍未完成，OTA 实现与配置不变。

### VDC-PROGRESS-20260915-027 — 非法命令目标位移修复及四板验收

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。
- 已确认缺陷：`refmem_sync_vdc_receive_frame()` 在校验 unicast target 范围前计算
  `1u << command.target_slot`，外层及 payload CRC 正确的非法目标也会触发未定义
  位移。现在先用 `REFMEM_SYNC_NODE_COUNT` 判断范围，再以短路表达式计算 mask；
  广播及正常目标继续沿原校验路径。
- 反例：新增 `test_vdc_invalid_targets_preserve_retained_command()`，先保留一条
  有效命令，再输入越界、位宽边界及接近整数上限的目标，核验逐次拒绝、原命令序号
  和 accepted count 不变。`-fsanitize=shift -fsanitize-undefined-trap-on-error`
  在修复前触发 trap，修复后退出成功；原始结果见 `target-before.json` 与
  `target-after.json`，早期测试构建失败日志也保留。这证明真实健壮性缺陷，不能
  倒推它就是历史零分片/prepare 拒绝的原因。
- 独立验收：完成相关 host 和 `pico2-release` 构建后，单独运行
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run --tdma-only
  --build-dir out/build-after-command-gate --out-dir
  out/HardwareAcceptance/20260915/command-target-fix-r1/p3`。
  `passed/flow_completed/strict_gates_passed` 均为真，无 diagnostic failures；
  TDMA closed-loop/realtime gate 通过，`left_running=false`，四板 STOP 后原生
  记录交接通过。凭证源码指纹为
  `c23d0f0990585231fba103f3c8a8d7daf63c87bce75750d6835027e422426253`。
- 证据：`out/HardwareAcceptance/20260915/command-target-fix-r1/` 保存反例、源码
  副本、diff 与 P3 原件。通过后才进入下一项 resident 编译隔离；没有合并两项代码
  变化共用一轮 P3。命令启用态及锁相未验收，OTA 实现/配置和 NO5 未改动。
- 下一 gate：`VDC-CMD-002/003` 先保持隔离，关闭普通四板基线收敛切片；后续按
  TODO 依赖逐项补齐所有者、时间输入与交付证据。

### VDC-PROGRESS-20260915-026 — 未提交改动对账与普通四板基线隔离

- TODO task ID：`VDC-TDMA-001`、`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`、
  `VDC-CMD-004`、`VDC-CMD-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
- 用户要求：先列清未提交修改和实际阻塞，评估收益；低收益增量记录后局部回退或
  隔离。先保证四板基础流程，再逐项加回，每次功能变化必须先跑 P3，避免错误累计。
- 审计基点为 `7fa834dfe531a902380f4c842f2cf5013c9acb47`。以下数量为本轮快照，
  非事实源：开始时有内容差异的 tracked 文件为 28 个，增加 1440 行、删除 951 行；
  另有 7 个 status 显示修改但 Git 内容 diff 为空，8 个 untracked 文件。不能从 diff
  判断每块代码来自哪台设备或哪位作者，也不能把这些增量全部视为同一个 patch。
- 逐文件路径、类别、行数、SHA-256、完整 diff 和工作区副本保存在
  `out/HardwareAcceptance/20260915/uncommitted-audit-r1/`。其 `inventory.json`
  在本次发送隔离增加 6 行之后、本文更新之前生成；`before-switch-reconstructed/`
  仅移除本轮精确加入的开关和条件，按 LF 保存两份源文件，可复核隔离前后差异。
  未知 HTML 仅登记路径/散列，没有清理或修改；OTA 配置、另一设备单板成果均保留。

| 改动组 | 实际内容 | 处置及证据边界 |
|---|---|---|
| RefMem 命令 wire 与副本 | `refmem_sync_frame.h/.c` 增加 epoch/run、广播目标，payload 由旧版尺寸扩展；`refmem_sync.h/.c` 增加顺序分片重组、guard 和有界稳定复制。 | 保留实现及负测。wire 仍是待审原型，混用旧固件的兼容性不能由同版本四板 P3 推定。 |
| resident 发送/接收接线 | `distributed_refmem.h/.c` 新增 MASTER record prepare、分片发片、FOLLOWER 重组、会话清理和诊断；resident enabled 时抑制旧 standalone TX/RX。 | 本轮只禁用新发送选择；普通 compact TDMA 继续，不恢复旧 standalone VDC 命令。 |
| TDMA 元数据 | `tdma_process_image_layout.h` 增加命令 message class/fragment 布局；`tdma_ring_runtime.h/.c` 的 clock snapshot 增加周期、超时和 ring sequence。 | 保留；status 中其余 PIO/flight/adapter 文件无 Git 内容差异，不能归因为本轮新 PIO 行为。 |
| VDC/DPLL 时间应用 | `vdc_time_mapping.h/.c` 新增共同时间映射、新鲜度、到期/迟到、序列回绕函数；manager follower 消費由原 uptime 比较改为共同时间检查；domain 新增 late 计数。 | 这些正确性约束有价值；运输和映射尚未闭合，不能以关掉时间校验来取得 apply。 |
| 构建、SCPI、工具与测试 | CMake 加入时间映射文件；SCPI 和两个解析工具扩展诊断字段；RefMem、VDC、时间映射及 Python 测试同步。 | 双应用/Boot 构建通过，host 脚本 38/38；诊断字段本身不能证明新功能成功。 |
| 文档、凭证与外部文件 | TDMA TODO 大幅整理；VDC 方案/进度及索引变更；新增 RAM/review 文档；P3 receipt 更新；未知 HTML。 | 文档行数变化不等于运行路径变化。原始 P3 成败按各轮目录保留，未知文件不动。 |

- 已确认的第一处断点是 MASTER prepare 到 record/fragment 的交接；prepare 失败会
  回落普通 mailbox，不直接停止 TDMA。旧 `four-board-clock-diag/command-clock-readback.json`
  为 prepare 尝试/拒绝均 1228、reason 5；`four-board-clock-diag-r3/direct-window-readback.json`
  的 MASTER 为 1787/1787、reason 16；`four-board-clock-map-r5/direct-window.json`
  为 1785/1785、reason 15。对应四板 fragment/complete/accept 均为零。以上为历史
  原始读回快照，数字和枚举解释须绑定当轮源码，不将旧元组套用到新增字段后的布局。
- 原因边界：已有摘要把 reason 15 概括为“窗口已开始”；当前源码的 reason 15
  实际用于 `local_now_ns < local_rx_timestamp_ns`。窗口顺延和 fresh-now 映射虽已在
  原型中修改，但缺少最新版本的命令成功原件，不能据此宣称根因已修复，也不能从
  零分片推定 CRC 或接收器已有故障。此前独立启动 barrier 超时同样保留为未解决事实。
- 价值判断：共同时间身份、新鲜度、CRC、会话取消及有界跨核读取必须保留。当前低
  收益做法是把尚未完成的定时应用依赖与发送准备同时接入，再反复运行只覆盖 TDMA
  基础流的 P3；P3 通过无法定位命令层错误。当前隔离该原型，后续每步除 P3 外还需
  对应的正向功能读回，首先证明完整接收，再证明定时应用。
- 单项代码变化：在 `distributed_refmem.h` 加入
  `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，当前关闭；在
  `distributed_refmem_tdma_flight_sync_publish()` 的 `resident_master` 条件中短路。
  校验和旧会话保护继续存在，未绕过 HAOFV owner，未启用从板本地 PI。该变化是
  发送功能隔离，不是对 HEAD 的全量源码回滚，更不是四板锁相完成。
- 软件验证：`python tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release
  --build-dir out/build-after-command-gate` 通过，build `20260915022619`；编译容量
  `PROJECT_NODE_CAPACITY` 保持当前配置，实板使用四节点。`powershell -NoProfile
  -ExecutionPolicy Bypass -File tools/tests/run_host_unit_tests.ps1` 通过 38/38。
  使用 Windows PowerShell 是因为该 host runner 为 `.ps1` 且环境无 `pwsh`。
- 已复核的隔离前 P3：`four-board-clock-map-r5/` 及
  `four-board-command-map-r6/diagnostic.json` 的四板 quick 结果通过；r6 为
  `passed=true`、`strict_gates_passed=true`。这已经说明基础 TDMA 可以通过，不能把
  禁用命令之后的成功反过来当作“命令代码曾造成基础 TDMA 故障”的因果证据。
- 本次隔离的 P3：`python tools/hardware_acceptance/p3_hardware_acceptance.py run
  --tdma-only --build-dir out/build-after-command-gate --out-dir
  out/HardwareAcceptance/20260915/four-board-baseline-command-disabled-r1` 通过。
  `acceptance.json` 的 source tree SHA-256 为
  `bf2f19e9ad51f0a062accd5ebd6ae9a423bf785737b6eebc08a9477e6163a15f`；
  `diagnostic.json` 为 `passed=true`、`flow_completed=true`、
  `strict_gates_passed=true`、`failures=[]`。保留 quick profile 的
  `diagnostic_continue=true` 事实，不提升为产品验收或 formal lock。
  TRN-03 的 realtime/closed-loop/diagnostic/startup barrier/soak 均通过，
  `left_running=false`，四板 STOP 后原生记录均可读；原件与散列见
  `tdma-stopped-handoff.json`。`timing.json` 的总预算计时约 188.5 s（快照，
  非事实源），含四板部署；NO5 未参与。
- 文档及工具验证：docs_check 的 strict names、doc_regression 均通过；文档检查器
  与 DPLL capture Python 回归合计 36 passed。原有 `TDMA-FLIGHT-BITMAP-01` 格式
  WARN 保留。`check-staged` 当前返回 no staged code change，只表示本轮没有暂存
  源码，不冒充提交指纹验收；本轮未提交。
- 独立只读复核：`command_gate_audit` 认可该变更作为普通四板 TDMA 发送隔离，
  不认可将它描述为完整 VDC 回退。还指出恢复时须核对
  `(REFMEM_SYNC_VDC_FRAGMENT_COUNT + 2u) * schedule.period_ns` 的提前量是否
  覆盖实际发布间隔和 FIFO 背压；这是待验证风险，尚不是实测首个阻塞。
- 下一 gate：沿 TODO 的时间输入/角色/契约前置条件推进；每个后续增量分别完成
  P3 和功能证据，不一次重新打开全部命令逻辑。命令 apply 与锁相继续未完成。

### VDC-PROGRESS-20260915-025 — common-time/command-record 阻塞诊断贯通

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-TDMA-001`；状态 IN PROGRESS。
- 变更：将 TDMA adapter 的 observation/build 计数与最近拒绝原因贯通到 runtime
  snapshot，并新增只读 `SYSTem:TDMA:RING:CLOCK:DIAGnostic?`；将 MASTER
  command-record 的 prepare 尝试、拒绝原因、record 活跃/分片位置和最近 effective
  common time 加入 `SYSTem:REFMEM:SYNC:FLIGHT?` 尾部字段。运行时仍只在既有
  Core1/TDMA 边界更新，SCPI 只在 STOP 后读取。
- 软件验证：`pico2-release` 固件双应用、Boot、USB namespace、flash map/link 检查
  全部通过；build identity 为 `20260915012243`。本切片未改变 OTA 配置。
- 当前解释：四板上一轮 `vdc_command_fragment_rx/complete/accept=0` 只能说明
  command record 尚未发车；下一次四板短窗应先读取 prepare last reason，再区分
  common-time mapping 拒绝、window/identity 拒绝和实际 fragment transport 缺口。
- 硬件状态：尚未用本次 build 重做四板 P3；不得据此宣称 command apply、DCO 跟随或
  formal lock。下一 gate：当前源码指纹下重新完成四板 quick P3，再用新增字段定位
  首个真实拒绝原因。

### VDC-PROGRESS-20260915-016 — resident VDC command fragments and current-source P3

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`；状态 IN PROGRESS。该切片
  只验证固定 process image 上的受限 resident 运输原型，不冻结共同时间、过期策略或
  正式 follower application 契约。
- 实现：新增 `TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS`。Core0 将完整 VDC
  command payload 按 `REFMEM_SYNC_VDC_FRAGMENT_COUNT` 分片，mailbox 保留 source、
  target mask、`uint16_t` transport sequence、fragment index/count 和固定数据区；
  follower 逐片校验 CRC、source、target、index/count 与回绕连续序列，完整重组后再
  通过既有 frame/payload CRC、epoch/run 和 command sequence 准入。完整命令由
  `vdc_command_guard` 保护，Core1 经 `refmem_sync_vdc_copy_command()` 有界复制。
  STOP/ARM、role、epoch/run、schedule CRC 和 control generation 变化会清理组装状态。
- 读回：`SYSTem:REFMEM:SYNC:FLIGHT?` 末尾追加 fragment RX/complete/reject、command
  accept 和最后 command sequence；`tools/tdma_ring_monitor/flight_bitmap_validate.py`
  已同步字段表，供 STOP 后统一读回，不用于实时采样。
- 软件验证：`powershell -ExecutionPolicy Bypass -File tools/tests/run_refmem_sync_tests.ps1`
  通过；host fragment/seqlock/负测共 21 项通过。目标构建
  `cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、USB namespace、
  flash map/link 检查；随后 `powershell -ExecutionPolicy Bypass -File
  tools/tests/run_host_unit_tests.ps1` 的 37/37 host unit test scripts 通过。
- P3：加入 SCPI 只读计数后按当前源码重新运行
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run`，build `20260915001009`
  仍在五板 OTA 阶段因缺失序列号 `839E1AE79EA20F31` 失败；本轮原始证据保留于
  `out/HardwareAcceptance/20260915/p3-081003/`，前一轮 `p3-080313` 也保留，不能以旧
  receipt 替代当前源码验收。
- 边界：当前四板实际 fragment RX/complete/accept、effective time 对账和 DCO 输出
  跟随尚未取得；不能据此宣称共同时间、正式 LOCKED 或四板锁相。另一设备的单板改动
  保持未提交，OTA 保持不变。
- 下一 gate：恢复缺失板卡后重跑当前源码 P3；随后在四板 STOP 后读取 fragment
  receive/complete/reject、command accept、source/generation/schedule CRC/sequence
  和 DCO application 计数，再决定 `VDC-CMD-001` 是否具备独立交叉审核和登记条件。

### VDC-PROGRESS-20260915-017 — common-time deadline mapping and session fence

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条
  仍是实现切片，不改变 `VDC-CMD-001` 的独立审核状态。
- 命令 payload 增加 `epoch_id`/`run_id`，接收端同时校验 frame header 与 payload 的
  session identity；旧 session 即使 source、schedule 或 command sequence 重用也会被
  拒绝。payload 尺寸变化由 `sizeof(refmem_sync_vdc_command_payload_t)` 驱动分片计数，
  没有新增独立同步帧。
- VDC owner 新增有界 `vdc_dpll_manager_map_local_to_common_time()`：只接受当前
  schedule CRC 下的 hardware-latched、COMMON_TIME/CYCLE_PHASE observation，并以
  `feedback_timeout_ns` 限制 anchor age。MASTER 先将本地 TDMA window 映射到共同时间
  再生成命令；FOLLOWER 以同一映射比较 deadline，映射无效时保持上一可信 DCO 输出。
- 验证：`cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、flash
  map/link 检查；`run_refmem_sync_tests.ps1` 通过，并新增旧 session 拒绝负测。全量
  `run_host_unit_tests.ps1` 为 37/37 通过。当前源码 P3 build `20260915002136` 已重跑，
  仍在五板 OTA 阶段因缺失序列号 `839E1AE79EA20F31` 失败，原始证据在
  `out/HardwareAcceptance/20260915/p3-082128/`。
- 下一 gate：补齐 mapping 的 C/host 边界负测（无锚、过期锚、schedule/session mismatch、
  overflow），然后恢复缺失板卡重跑 P3；四板上对账 common deadline、实际 apply 时间和
  DCO 读回后，才进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-018 — latest-source validation rerun

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录验证结果，不改变 wire 契约、锁相状态或 C11 审核状态。
- 软件验证：重新执行 `cmake --build out/verify/vdc_resident_fragment`、
  `run_refmem_sync_tests.ps1` 和 `run_host_unit_tests.ps1`；构建检查通过，refmem sync
  通过，全量 host unit 为 37/37。
- 当前源码 P3 已再次运行，build `20260915002546` 在五板 OTA 阶段失败，唯一报告为
  `missing_serial_numbers=839E1AE79EA20F31`；原始证据保留于
  `out/HardwareAcceptance/20260915/p3-082540/`。该结果不能替代缺失单板恢复后的五板
  验收，也不能推导四板锁相或 `FORMAL_LOCKED`。
- 下一 gate：补齐 mapping 的 C/host 边界负测（无锚、过期锚、schedule/session mismatch、
  overflow），恢复缺失板卡后重跑当前源码 P3，再执行四板 STOP 后 fragment/command/DCO
  对账；在此之前 `VDC-CMD-001` 继续等待独立 C11 交叉审核。

### VDC-PROGRESS-20260915-019 — stateless common-time mapping boundary gate

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录边界验证切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：新增 `vdc_time_mapping_map_local_to_common_time()` 无状态映射模块。VDC owner
  先复制 TDMA ring clock snapshot，再调用该函数；函数拒绝空快照、未启用/未启动环路、
  无效周期或反馈超时、schedule CRC 不匹配、无关联硬件 latch、时间倒退、过期 anchor
  和共同时间加法溢出。Core1 的调用仍保持有界，不增加解析、存储或等待。
- 软件验证：新增 `run_vdc_time_mapping_tests.ps1`，覆盖有效映射和上述拒绝边界；专项
  测试通过。固件 `cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、
  USB namespace、flash map/link；全量 host unit 为 38/38；`test_dpll_vdc_monitor.py`
  为 24/24。
- 当前源码 P3 已重新运行，build `20260915003253` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-083247/`。这不是四板共同时间或锁相证据。
- 下一 gate：缺失单板恢复后重跑当前源码 P3；随后四板 STOP 后对账 fragment、command、
  common deadline、实际 DCO apply 和输出记录，才进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-020 — receiver-owned late command retirement

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录定时应用的迟到处理切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：新增 `vdc_time_mapping_classify_effective_time()`。FOLLOWER 在同一份 TDMA
  snapshot 上先完成共同时间映射，再按当前激活的 `cycle_period_ns` 判定命令为提前、
  窗口内到达或超过最大迟到窗口。提前命令继续保留等待；窗口内命令进入唯一 DCO
  应用者；超过一个周期的命令以非法命令退休、记录计数并保持上一可信输出，发送端无法
  通过 wire 字段放宽该门禁。该边界随 STOP 后周期配置变化，不增加 mailbox 字段。
- 软件验证：映射专项测试通过，新增提前/准时/迟到/非法输入边界；全量 host unit 为
  38/38，固件双应用、Boot、USB namespace、flash map/link 构建通过。
- 当前源码 P3 已重新运行，build `20260915003820` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-083813/`。该结果不构成四板实际应用或锁相证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板运行时需对账提前、准时和迟到命令的
  接收/退休计数、common deadline、DCO apply 及实际输出，再进入 `VDC-CMD-001` C11
  交叉审核。

### VDC-PROGRESS-20260915-021 — command sequence wrap continuity

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`；状态 IN PROGRESS。本条只记录命令序列
  回绕切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：FOLLOWER 的实时命令准入改用有界 signed-difference 序列比较，避免
  `UINT32_MAX -> 1` 回绕后的新命令被简单数值比较误判为陈旧；零序列、重复和真正倒退
  仍拒绝。逻辑位于无状态映射/准入辅助模块，未增加 Core1 等待或 mailbox 字段。
- 软件验证：新增 sequence 首值、重复、倒退、回绕和零值负测；映射专项测试通过，固件
  构建通过，全量 host unit 为 38/38。
- 当前源码 P3 已重新运行，build `20260915004220` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-084212/`。该结果不能替代四板命令接收或锁相
  证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板需在实际序列回绕、迟到和 STOP/ARM
  场景下读回接收/退休/apply 计数及 DCO 输出，再进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-022 — resident transport wrap regression

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`；状态 IN PROGRESS。本条只补充运输层回绕
  证据，不改变 wire 契约或审核状态。
- 测试：`test_refmem_sync.c` 新增独立 VDC context 的命令序列回绕负测，先接受
  `UINT32_MAX - 1`，再接受 `1`，确认完整帧、payload CRC、session、来源和目标校验均
  通过且最新命令被保留；重复/倒退规则仍由既有测试覆盖。
- 验证：`run_refmem_sync_tests.ps1` 通过。该切片只改 host/real-C 测试，不替代板端
  resident 接收和三从 DCO apply 证据；上一条当前源码 P3 原件仍为
  `out/HardwareAcceptance/20260915/p3-084212/`。
- 下一 gate：缺失单板恢复后，以当前固件重跑 P3，并在四板实际 transport sequence
  回绕和 command apply 场景中完成 STOP 后计数对账，再推进 `VDC-CMD-001` C11 审核。

### VDC-PROGRESS-20260915-023 — explicit late-command readback

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。本条
  只增加迟到处理的板端可观测性，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：Domain 新增 `follower_late_command_count`，仅由 Core1 的超过最大迟到窗口
  分支递增；`SYSTem:SYNC:VDC:DPLL:ROLE:STATus?` 在原字段末尾追加该计数。离线
  `dpll_observation_capture` 解析器和回归测试同步更新，因此 STOP 后可以区分非法命令、
  陈旧命令与明确的迟到退休。
- 软件验证：`run_vdc_domain_tests.ps1`、`run_host_unit_tests.ps1`（38/38）以及
  DPLL/VDC Python 回归（42/42）通过；固件双应用、Boot、USB namespace、flash map/link
  构建通过。
- 当前源码 P3 已重新运行，build `20260915005037` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-085031/`。该结果不构成四板计数或锁相证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板 STOP 后必须读回 late/invalid/stale/
  apply 与 resident fragment/command 计数，再进行 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-024 — 四板当前源码 P3 与 resident command apply 对账

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`、`VDC-VERIFY-001`；状态
  IN PROGRESS。本条只记录四板真实验证和阻塞定位，不改变命令契约、registry 状态或
  锁相结论。
- 四板 P3：使用当前源码 build `20260915005602`，四块板为
  `0010071E65B5CB38`、`FB276192BEF9CCE1`、`2BD5090FE009FA2A`、`A1E549202D18ED6A`。
  `run --tdma-only` 完成四板 OTA、P0T、校准、TRN-00/01/02/03、process-image/FIFO
  和 STOP 后冻结记录；receipt 的 `strict_gates_passed=true`、`failures=[]`，不含 NO5
  DPLL 观测。原始证据：
  `out/HardwareAcceptance/20260915/four-board-tdma/`；该 receipt 属于
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，不能提升为正式锁相。
- 四板命令应用试验：复用同一 TRN-03 矩阵，以一主三从启动短运行窗口；运行期间未做
  串口采样，STOP 后统一读回。证据位于
  `out/HardwareAcceptance/20260915/four-board-command-apply-r2/`，原始对账为
  `command-apply-readback.json`。NO1 读回 MASTER，NO2--NO4 读回 FOLLOWER/source
  slot 0，四板 STOP 响应均为 `OK`。
- 结果：TDMA resident process-image 的普通 transport 计数继续增长且没有 transport
  reject；但四板 `vdc_command_fragment_rx_count`、`vdc_command_fragment_complete_count`、
  `vdc_command_accept_count` 均保持为零，三块从板的 `follower_apply_count` 也未增长，
  DCO follower 快照保持无有效更新。该结果说明当前共同时间/窗口准入尚未让 MASTER
  生成并发车 resident VDC command，不能把普通 TDMA 通过误称为命令应用通过。
- 失败边界：TRN-03 独立短窗的 startup barrier 仍超时，但 `left_running=true`；随后已
  由主控发送四板 STOP 并完成读回。该诊断失败和所有原始快照保留，未修改 OTA、未操作
  NO5、未清理另一设备的单板修改。
- 下一 gate：先从 `vdc_dpll_manager_map_local_to_common_time()` 的 hardware-latched
  observation/anchor 失败路径入手，补充运行态 TDMA status 的 common-time、correlation
  flags 和 resident command-record 是否建立的 STOP 后证据；修复或证明该准入后，再重跑
  四板 fragment/command/apply/DCO 对账，随后才提交 `VDC-CMD-001` 独立 C11 交叉审核。

### VDC-PROGRESS-20260915-015 — RX scan/drop 根因审计与验证基线复核

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。本条只记录当前源码审计和软件验证
  基线，不改变 TDMA/VDC 契约，也不授予 observer、首帧或锁相资格。
- `tdma_pio_spi_phys_rx_scan.inc` 当前在 `produced - scan_produced > keep` 时将游标前移
  并递增 `rx_observation_drop_count`；`keep` 由调用方 `max_words` 加观察扫描余量构成，
  因而在两帧已完成而单次请求上限小于物理帧跨度时，会把“为保护环形覆盖而丢弃旧前缀”
  与“首帧交接丢失”合并计数。该计数不能直接解释为物理首帧丢失，下一切片必须先把
  backlog/clamp、scan cursor 和 observer 首 ordinal 分开记录，再评估是否需要增大保留窗口；
  不放宽 sequence 或身份门禁。
- 当前全量 host/real-C 回归基线为 1655 passed、1 skipped、2 failed、15 errors。失败/错误
  集中在已有的 event-service/fixture 提取和 latch/origin 组合测试（例如 fixture 未提供
  `tdma_rx_start_cut_monitor`、`tdma_pio_spi_phys_event_selected`），未形成新的生产硬件证据；
  该结果保留在 `out/pytest/runs/`，不能用来替代本轮 52 项专项通过结果。代码切片未提交，
  不覆盖另一设备的单板修改。
- 下一 gate：继续 `VDC-TIME-002`，先为 scan backlog 建立不增加实时等待的分层计数和真实 C
  负测，再回到“STOP 完整退休后冻结 geometry、下一 ARM 显式 generation 选择”的 observer
  预启动实现；`VDC-TIME-003/004`、DPLL 正式锁相仍保持 PENDING。

### VDC-PROGRESS-20260915-014 — 板端有限发帧与最早原始前缀保留

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-bounded-burst/`。以下数字均为本轮快照，
  非事实源；本切片关闭有限发车及最早 raw 前缀保留，不授予 packet/event 身份或锁相。
- `TDMA_RING_FLAG_DIAGNOSTIC_BURST_MASK` 在既有 config.flags 中编码下一 ARM 的诊断
  额度。`SYSTem:TDMA:RING:BURSt` 只在 STOP applied 后配置，零保留普通无限行为；
  非零只允许 diagnostic reference 的 physical process-image。Core1 adapter 在
  phys_tx 接受后、FSM 操作前记账；耗尽后继续完成与 RX，跳过 TX 准备及自主准入。
  未增加 PIO 指令、SM、DMA、帧缓冲或 Core1 等待。
- 重复 START 不填充额度；TRAIN、adapter/runtime/physical 自主入口均拒绝有限模式。
  STOP 保留计数，新 ARM 成功才重置；ARM 失败使 diagnostic_burst_valid 失效，避免
  新配置与旧计数混用。独立审查发现的物理公开入口防御缺口及失败 ARM 读回混配已修复，
  `reviewer-code-r1.json` 支持上述代码边界；不将耗尽或 STOP 后 pending 清零当完成。
- 最终 `host-r2` 52 项通过，包含真实 C adapter/runtime、Core0 生命周期与实际 SCPI
  callback，覆盖忙时拒绝、物理已发后 FSM 失败、同序号 bootstrap、取消重臂、失败
  ARM、重复 START 与自主互斥。早期 host-control-r1/build6-r1 在复核修复前已通过，
  保留原件；最终构建为 build6-r2/build8-r1。
- 6/8 节点 A/B 构建通过，静态 RAM 增加 8 B，扣 heap 后余量为 17148/13388 B；
  SCRATCH_X 数据仍为零。源码指纹为
  `254f0fd49d2a3ea93550164f57ea9e9cb2ddf2f96611b21448c8982e7fe265ac` / 1098；
  六节点包 SHA 为 `f40c893ceaf3c16881bf698edc17c231b17efc760e31e6fd6640ddd2c2f89c99`。
- 当前四板 P3 用时 178.171 s，quick passed、strict_gates_passed=true；普通短帧
  passed/closed_loop/realtime_gate/diagnostic 全 true。STOP 后四板 native SD 读回与
  SRAM 逐字节一致。本轮仍为整表 1500 µs、TDMA 预算 850 µs，不证明 500 µs 达标，
  也不追认 013 的严格校准失败或完成 DPLL 验收。
- 专项预声明主板额度为两次：全板 ARM 无 START→STOP/ACK→重臂，从板预置采集，
  主板 START 后留复制时间，再重复 START，最后全板 STOP/ACK。主板实际接受/完成
  均为两次，TX timeout、clock/data timeout、recovery 无增长；RX_GATE_REJECT=7
  原样保留。三从各自 pre-produced=0、capture-produced=346、retained=346、最终
  produced=346，F=173，首 raw 坐标为零，复制期间没有后续流量覆盖。没有 RUN 查询。
- 最早 raw 前缀已经保留。固定 `[0,F)`、`[F,2F)` 分区各得到一个 168 B 合法
  packet，三板两帧内容 SHA 一致，sequence=1、hop=0，identity/transport CRC
  原样通过。header 起点分别为 24/13/2 bit，对应本轮 physical A/b=3/0、1/5、0/2；
  这些偏移是位相几何，不是首帧丢失。原始证据与输入哈希见
  `first-prefix-diagnosis-r1.json`。这仍不证明同步 CS 绝对锚、observer ready 或
  连续 event 身份：每板 `rx_observation_drop_count=1`、scan produced 小于 DMA
  produced，DPLL trailer=0，quota2 重复 sequence=1 也不能替代连续性。
- 三从通用 SAVE 再次在各自 12 s 截止时仍 RUNNING，专项 exit=1 保留。之后只读确认
  同一 job 7 已 DONE，原文件 2110/2126/2144 B 成功下载，node/build/generation/epoch
  核对一致；没有重新 SAVE、重新采样或延长原截止门。保存延迟原因仍未确定。
- 下一 gate：继续 `VDC-TIME-002`，将已验证 A/b 作为本轮 observer provenance，先处理
  scan/drop 和重复 bootstrap 的连续性，再实现有代际的训练几何冻结及 ARM 前 observer
  准备。不能复制旧
  DMA 原点的 A/b 后假置 alignment locked，也不能以两帧有限诊断代替连续 resident
  闭环。TIME-003/004 仍 PENDING，正式时间映射、命令应用及实际输出锁相继续未完成。

### VDC-PROGRESS-20260915-013 — 首次 capture CS 保护与首帧采集缺口

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。设计证据位于
  `out/HardwareAcceptance/20260915/dpll-observer-prelaunch-design-r1/`，实现、测试与
  硬件原件位于 `out/HardwareAcceptance/20260915/dpll-initial-cs-gate/`。以下数值
  均为本轮快照，非事实源；本切片未授予正式时间戳或 packet/event 身份。
- 独立设计审查支持先保护首次 capture：原程序直接进入 WAIT SCK，CS 高期间不足
  一字节的 SCK 也可能改变位相而 DMA 仍为零。`tdma_pio_spi_phys_arm()` 在全部
  seed/PULL/latch rearm 后、enable 前，仅对 process follower 注入一次 WAIT RXCS
  low。未增加 CPU 等待、静态状态、PIO 指令槽、SM 或 DMA。既有 STOP 成功退休后
  restart 清除挂起指令；CS 已低或 disabled 期低脉冲仍可能消费 WAIT，不授予 clean。
- 完整预启动路线仍需独立冻结训练几何及稳定配置/时钟/校准绑定，复用 config 发布
  链与既有全板 ARM ACK。物理 owner 在 START 前已有服务机会；不得抢占周期专用的
  stopped_update token，也不得假置 overlay alignment locked 来绕过训练。
- 软件：`host-r2` 336 项通过；真实 C PASS 命令与组装/重定位 PIO 模型覆盖首次 CS
  前的零散时钟、完整首字节和后续帧。三个负例分别在帧外采样、首位相位偏移、缺失
  首位时精确失败。模型不覆盖 disabled 期脉冲、跨板同步器、DMA 延迟或目标 PC 恢复。
  `review-code-r1.json` 无源码/模型阻塞；目标首帧资格明确保留。
- 6/8 节点 A/B 构建通过，静态 RAM 增量为零，扣 heap 后余量 17156/13396 B，
  SCRATCH_X 数据为零。源码指纹为
  `8911a9576f2b59f6ee62a4a71c89cf947a7622886e0ec25ab5abb4661f41bca8` / 1097；
  六节点包 SHA 为 `ebf1daef696679f448427c6d827a5ba79a9e25622477daa792edcbc1f76d48a3`。
- 当前 P3 流程耗时 178.079 s，quick passed，但 strict_gates_passed=false：保留
  NO2 粗 CLK 校准的 OPMODE APPLY 超时及 Execution error。后续普通短帧
  passed/closed_loop/realtime_gate/diagnostic 全 true；四板各 14 槽及 baseline，
  missed/reason 为零，STOP 后顺序 SD 读回与 SRAM 逐字节一致。实际整表为 1500 µs、
  TDMA 预算 850 µs，不能称 500 µs 达标。没有专用 ARM 耗时测量，不用稳态峰值代替。
- 首帧专项执行了全板 ARM 无 START→STOP/ACK→重臂；采集前三从 capture PC 为 4、
  DMA produced 为零。三从预置 SCK 采集后，主板短时 START/STOP，从板继续复制，
  全板 STOP/ACK 后才查询/保存。RUN 无 SCPI 查询，NO5 未操作。
- `first-frames-r1.json` 未通过首帧资格：主板 START/STOP 响应分别为 62/63 ms，
  实际完成 22 帧；三从快照定界时 produced=1038，仅保留 512 B，首坐标已为 526；
  最终发布 produced=3806。首帧已丢失，复制期间也不能排除覆盖。没有重采挑窗。
  SCK 样本为 256×4 ns，首高样本为 8/11/11；这只是采样起点后的偏移，没有同步
  CS 样本与物理 ARMED 回执，不能称精确 CS→首 SCK 门限或首帧 DATA 通过。
- 通用 capture SAVE 在各自 12 s 截止时仍 RUNNING，原件保留。之后一次 STOP-only
  恢复读取确认原 job 均 DONE，无再次 SAVE，三份原生 SD 文件成功下载并核对
  generation/epoch。恢复不追认先前截止门；通用保存延迟原因尚未确定，不能归因 OTA。
  `analysis-r2.json` 使用真实 realtime_gate_passed 字段，r1 的空字段保留并更正。
- 下一 gate：仍在 `VDC-TIME-002` 内先实现或复用 owner 有限发帧/最早帧保留能力，
  补齐首次 DATA、CS/SCK 建立时间、STOP 取消及目标恢复证据，再进入冻结几何与早启
  observer 集成。不得在健康 RUN 暂停/abort DMA 强求边界；串口 ACK 不能决定帧数。
  本次普通环路通过不关闭首帧验收、严格校准、身份或锁相缺口，TIME-003/004 仍 PENDING。

### VDC-PROGRESS-20260915-012 — 首帧坐标证明与启动路线收敛

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-coordinate-proof-r1/`。本轮仅做只读
  源码/官方硬件文档审计及离线数学模型，未修改固件、PIO 或执行新硬件采集。
  以下数量与页码为快照，非事实源；011 已分离提交并封存，520 件文件独立复验通过。
- 条件性结论：若两次完成写计数处于同一已证明 idle、未完成字数上界为 `Q_i`，
  且物理边界余数 `r` 与跨度 `F` 独立已知，则边界位于
  `[max(C_i), min(C_i+Q_i)] ∩ (r+Fℤ)`。区间宽度小于 `F` 时至多一个候选。
  `coordinate-interval-model.json` 的 189800 组有限整数枚举通过，并用六份原始
  cut 做假设上界敏感性分析；模型没有证明目标硬件的 Q、idle 或 r，不能直接准入。
- 官方 RP2350 datasheet §12.6.4.2 说明 DREQ credit 在 transfer issue 时扣除；
  已读出 FIFO、尚未完成 SRAM 写入的数据仍在内部流水线。外设 FIFO 深度及 credit
  寄存器宽度不能直接当作总 Q；§12.6.7.3 的错误后抑制/地址偏移上界也不能当作
  正常流水线总容量。官方 PDF、哈希和逐页摘录保留，本轮未获得足以关闭 Q 的证明。
- 当前绝对 PC 5 为 capture 程序反复执行的 WAIT SCK high；IRQ3 为 service
  读后清的粘性观察，不是精确物理计数。当前完成写数已超过一帧，在合法初始命令及
  无重启等前提下可证明曾经过首个 terminal；仍须证明其后没有漏钟、额外采样或
  命令停顿才能归纳边界余数。启动 cut 的 SM2 RXSTALL/TXSTALL/RXUNDER/TXOVER
  均清，但当前运行监测只将其中 RXSTALL 纳入退休，不能据此证明整个后续窗口。
- 时序缺口：首次 DMA count 在 start_pad_before 之前读取，末次 count 在
  start_pad_after 之后读取。即使排除两个高电平 pad 样本之间藏入完整帧，也不能
  自动把外侧两次 count 限定在同一 idle；首读可能仍在前帧尾，末读可能已到下一帧。
  必须补齐见证或另建包含新增输入字数的保守模型，不能把当前等计数当作精确边界。
- 后继优先评估受控首发路线：训练后完整 STOP/取消，冻结与当前配置/时钟代际绑定
  的几何描述；ARM 阶段先使从板 capture 和 observer 就绪并证明零起点，再允许
  origin 首次发帧。复用现有全板 `ARM_CONFIG_APPLIED_ACK` 和 started barrier；
  本轮原件已有全板 ACK，不重复建设第二套确认。现有 START 文本响应不能替代
  observer 就绪证明，现有 observer 仍在训练后晚启用，旧窗口不能追认零起点。
- 下一 gate：完成该 ARM 前准备方案的 owner 状态、代际、静默、取消和迟到拒绝
  审查后，才实现最小诊断切片。不得在健康 RUN 中暂停/abort DMA 来强求边界；
  不新增忙等，不借用其他域 PIO/DMA。当前身份、正式时间戳和锁相仍未证明，
  `VDC-TIME-003/004` 保持 PENDING；本设计不冻结跨域契约。

### VDC-PROGRESS-20260915-011 — 首次 observer 启用的 DMA 坐标括号

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-start-cut/`。以下数量、时间、容量与
  代际均为本轮快照，非事实源；本切片只补充诊断，不授予物理身份或时间戳。
- TDMA owner 在首次实际 observer enable 两端记录 DMA completed-write count、
  epoch、计时、capture FIFO/PC/pad/fdebug 和配置。使用真实 counter 的局部副本
  lift 坐标，不推进 live scanner/drop accounting；capture RXSTALL、DMA 故障、
  epoch/geometry/role/clock 变化或 observer 失效只退休 cut。首次 STOP 后终止
  运行监测，完整 disarm 的两项取消 ACK 成功后冻结 STOPPED；新 ARM 先清旧代。
- `tdma_rx_start_cut_t` 为 128 B，owner、双缓冲及控制标量共新增静态 RAM 396 B。
  `SYSTem:TDMA:FLIGHT:RX:CUT?` 通过有界一致性 getter 读取冻结副本；getter
  不读 live PIO/DMA，但读取系统计时器。全板 UID/build/STOP/config ACK barrier
  后每板只查询一次，原始错误不重试。cut 单独导出到主机，不在 native SD 记录内；
  通用 event snapshot、原生每槽 schema 和 PIO/DMA 分配保持原样。
- 软件：`host-export-r2` 13 项、`host-adjacent-r1` 163 项、`host-owner-r1` 4 项、
  `host-cut-unit-r2` 1 项通过；覆盖真实启用/service/STOP、feature OFF、真实
  counter/lifecycle、并发读、SCPI 序列化及全板 STOP barrier。失败原件与 fixture/
  host facade 修正见 `tooling-notes.json`，不称生产内存协议因此被修复。
- 构建：6/8 节点 A/B 均通过，用时 18.953/16.828 s；源码指纹为
  `8654c754bdc2cc39f2f4f0c2a256649028e23ad4b4cfb81c6c085ba8305d13e1` / 1096，
  六节点 package SHA 为 `6c96a3f6290e6e7b9aeff406f71d7b846709f08d3df2739c423b6f4609b95074`。
  四份 map 净增均为 396 B，6/8 节点余量 17156/13396 B（已扣 heap）。DATA/BSS
  间隙仍 16 B，SCRATCH_X 数据为零；monitor/getter/SCPI 回调自身栈 96/48/152 B，
  新函数在 XIP。该局部栈不代表完整调用链或 WCET；归档 ELF 的内联启动顺序另审。
- 当前 P3 用时 184.219 s，quick 与 strict 均 true、diagnostic_failures 为空；
  普通短帧 passed/closed_loop/realtime/diagnostic 全 true。四板各 14 槽加
  baseline 完整，SD 保存读回 8.188 s，逐字节及 SHA 与 SRAM 一致。三从全窗
  query/matched 增量为 678/675、693/691、707/705，其余为 unavailable，
  stale/missing/ambiguous 均未增长。本轮通过不追认 010 的严格失败，也不能归因
  cut 修复了校准。
- 普通 cut：NO1 为预期 UNAVAILABLE；三从均记录成功，flags 127，retire_reasons
  仅 STOP；observer epoch 1、ARM 15、DMA epoch 1/1，物理跨度为 173 words。
  alignment byte/bit 为 3/0、1/5、0/2；两端 produced 分别均为 519、346、346，
  FIFO 均零、PC 均 5，capture SM2 RXSTALL 未置位。整个启用括号耗时为
  12.040/12.028/13.736 µs，仅为该括号，不是完整 observer 启动或 monitor WCET。
- 唯一自主 `capture-r1` 用时 39.782 s、准入 ACCEPTED/epoch 36，config/applied
  初末 70/70、model 初末 24/24；NO1 slots 3–17 为 persona 16，STOP 自主相位
  累计 6397。四板各 18 槽加 baseline 完整、missed/terminal reason 均零；SD
  保存读回 8.000 s，与 SRAM 字节/SHA 一致。RUN 无 SCPI 查询采样。
- 自主窗口三从 event epoch 2，published=joined 为 7098/7180/7257、无 INVALID，
  最大服务间隔 1576/1586/1579 µs。全窗 query/matched 增量为 1894/1891、
  2109/2107、2379/2377，余项仅 unavailable；这些增量包括准入前普通阶段。
  cut 的 observer epoch 2、ARM 16、两端 DMA epoch 1/1，STOP only，括号为
  12.044/12.028/12.044 µs。cut 在从板 observer 初启时取得、早于主板自主 grant，
  不能称自主切换边沿锁存；FIFO 空且 count 相等仍不能排除在途写或启用前积压。
- 自主总门仍失败，diagnostic 为 true；startup 三个健康样本在
  1.009730/1.510902/2.006068 s，第三个超过预声明门限。NO1 仍保留
  `adapter_tx_not_growing`、`physical_flight_persona_mismatch`，soak 保留
  `periodic_interval_gate_failed`；未重采挑窗或放宽门限。
- 时间反馈以 `timing-feedback-r2.json` 为准，r1 复制来的两条过时说明保留并纠正。
  按内部 total_ticks 选取的 PEAK 外层 NO1–NO4 为
  1578.616/1097.692/1076.596/1118.852 µs；NO1 超过整表周期。该记录是
  513→1537、trial 0→36 的自主准入迁移，010 所选记录为准入前迁移，两者不能
  隔离本轮 cut/monitor 成本；嵌套 stage 不可求和，PEAK 也不是全窗外层最大。
  完整 observer service_max_us 普通为 315/325/395、自主为 424/427/425，
  包含 startup 与完整服务，不等于 monitor 独立成本；全表 WCET 仍未闭合。
- 下一 gate：`VDC-TIME-002` 证明 pending DMA writes、prestart backlog 与唯一
  物理边界，再建立 capture coordinate 到 event ordinal 的关系；当前所有 cut
  恒为 unresolved、inflight unknown、prestart backlog unexcluded。物理身份、
  正式时间戳及锁相均未证明，`VDC-TIME-003/004` 仍 PENDING。

### VDC-PROGRESS-20260915-010 — 自主准入只读 foundation 身份

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-foundation-read/`。以下数量、时间、
  容量与代际为本轮快照，非事实源；本切片解除准入对无关诊断快照可用性的依赖。
- 实现：新增 `tdma_service_get_foundation_crc32()`，在既有 intent guard 下有界
  读取实际 CRC；首末 acquire、标量读取及中间 acquire fence 保证版本复验。
  上界沿用 `TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT`，不等待锁、不调用综合快照。
  空指针或尝试耗尽返回 false，并清零非空输出；成功返回的零值只是实际 CRC，
  不能当作配置有效或授权，模型匹配仍由原准入谓词验证。输出不能 alias service。
- Calibration 仅用该标量替代整个 `tdma_service_snapshot_t`。综合诊断快照函数、
  foundation 写入路径与 Core1 授权实现不变；保留 revoke-before-attempt、参数、
  角色、stage、cadence、余量、expiry、model 及最终 config/model 复验。Core1
  在准入、构造及运行边界仍比较实际 foundation CRC；无新增实时 PIO/DMA 工作。
  这有意改变读取可用性的依赖，不能称新旧函数在所有忙碌情形下返回行为完全相同。
- 软件：`host-admission-r1` 20 项通过，保留冻结旧函数的 47 场景 oracle，仅在
  owner 可用性等价时比较 grant、epoch、逻辑调用顺序和拒绝原因；另对全部场景
  注入无关快照不可用，验证实际门禁仍生效。真实 SCPI 回调在成功时才返回偶数
  epoch，失败保留拒绝并撤销旧授权；实际 Core1 stale/prepare 路径新增 foundation
  变化拒绝。getter 的锁与代际行为由独立生产函数测试覆盖，不由该 model stub 证明。
- 并发：`host-foundation-r2` 通过，五组、16 次成功检查使用真实 service/scheduler/
  registry/ring 结构与生产读取函数。实际 scheduler.lock 占用使综合读取失败，
  专用读取仍成功且不释放锁；result/registry/ring 的不可用不再影响标量读取。
  覆盖 NULL、零 CRC、odd guard、复制后 writer、持续代际变化、末次尝试及单次
  guard 回绕；耗尽最多 128 次 guard load，无陈旧输出替代。r1 漏链接
  `tdma_profile.c` 的失败原件保留，补真实依赖后通过；未声称实板争用时序已穷尽。
- 构建：6/8 节点 A/B 均通过，用时分别 15.890/15.250 s。当前指纹为
  `eaba0fbd9ec75f7276cb5b5b5301804204dd6b96950c4e29072ab98e925c28bd` / 1091；
  build ID 沿用 `20260914184059`，六节点 package SHA 为
  `e216cfffa428e20f3f1d7381d8c0abb1ce92c011770052b8d04ba7e1900ace5c`。
  `current-plan-r1.json` 的源码与构建归档分开绑定，不以 build ID 代替指纹。
- 资源：四份 map 静态 RAM 未增加，6/8 节点余量仍为 17552/13792 B（已扣 heap）；
  DATA end 为 `0x20008ff0`、BSS 为 `0x20009000`、SCRATCH_X 数据为零。
  实际 ELF 中 getter 位于 Flash，指令为 acquire load/scalar load/DMB/acquire
  load，SU 为 4 B。准入函数自身栈从旧 ELF 的 3088 B 降至当前 2288 B，减少
  800 B；综合快照自身为 936 B。仅为函数局部栈，不能推断完整 Core0/SCPI 栈
  高水位或 WCET。证据脚本路径选择和同名报告冲突另见 `tooling-notes.json`，
  成功编译/反汇编产物保留，未覆盖重跑。
- 当前 P3 用时 188.641 s，quick passed、普通短帧 passed/closed_loop/realtime/
  diagnostic 全 true；四板各 14 槽加 baseline 完整，STOP/ACK 后 SD 保存读取
  8.094 s，与 SRAM 一致。普通三从 query/matched 增量为 678/675、692/690、
  707/704；NO4 missing 增长一次，其他未匹配为 unavailable，不能将其删除或
  称逐包关联无缺口。稀疏快照未直接记录这次 missing 的单次现场。
- 严格失败：`strict_gates_passed=false`，NO3 coarse CLK level 7 的
  `SYSTem:TDMA:RING:TOPology 4,2,2` 返回 timeout/Execution error；coded marker
  的四个 trial 原件完整，但 NO4 best/second distance 同为 242、margin 为零，
  `mixed_peak` 门失败。两者均保留，不能以 ordinary passed 代替严格校准。
- 唯一自主 `capture-r1` 用时 39.782 s，准入 ACCEPTED，epoch 36，初末配置
  66/66、初末模型 22/22，observed_mask 为 1023；NO1 persona 从普通态转至
  16，并在 slot 3–17 保持，STOP profile 中自主相位计数为 6418。四板各 18 槽
  加 baseline 完整，terminal reason/missed 均为零；字节数依次为
  8592/11532/11600/11564 B，未满固定记录区。全板 STOP/ACK 后 SD 保存读取
  8.188 s，UID/build/epoch/CRC 可解码，SD 与 SRAM 逐字节一致；RUN 内无 SCPI
  查询采样，NO5 未操作。
- 原生诊断：三从 event epoch 为 2，published 为 7126/7192/7271，均与 joined
  一致、无 INVALID，最大服务间隔为 1586/1581/1585 µs。整个记录窗口的
  query/matched 增量为 1914/1911、2114/2112、2378/2376；剩余分别为
  3/2/2 次 unavailable，stale/missing/ambiguous 未增长。该增量包括准入前普通
  阶段，不能全部归为自主匹配；baseline 的旧 RETIRED 候选也不能当本次查询。
  候选与记录字段检查通过，仍是历史诊断，无物理身份、时间戳或 DPLL 正式资格。
- 总门仍失败：第三个健康 startup 样本最晚 2.009369 s，超过预声明 2 s；前两个
  健康样本在 1.007962/1.509057 s。NO1 还保留 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch`、`periodic_interval_gate_failed`。
  `analysis-r1.json` 诊断连续性为 true 仅证明当前记录范围；未追加采集、未放宽
  时间门、未以本次准入成功追认 009 或确定其具体失败子读取。
- 时间反馈：按内部 total_ticks 选取的 STOP PEAK，其外层相位耗时 NO1–NO4 为
  1443.404/1098.596/1045.568/1076.392 µs。NO1 该记录仍在自主 trial 前的状态
  迁移；其他板与 009 的流量模式不同，不由数值变化推断 getter 的独立性能收益。
  PEAK 不是全窗外层最大值，完整静态表 WCET 仍未闭合。
- 下一 gate：`VDC-TIME-002` 继续补齐物理 packet/event 身份、首事件/DMA anchor
  与晚启用积压排除；保留 startup、普通计数/persona 与严格校准失败分别追踪。
  `VDC-TIME-003/004` 仍 PENDING，不将完整诊断记录提升为正式时间输入或锁相。
- 后继只读设计：
  `out/HardwareAcceptance/20260915/dpll-rx-identity-design-review-r1/identity-design-review.json`
  核对当前 observer=ON 的 follower TX 为 32/32 指令、4/4 SM，RX 指令也为
  32/32；旧 observer=OFF 的 TX 余量不能借用于当前配置。建议下一切片先记录
  首次 observer 启用前后的 DMA 完成坐标区间及 capture RXSTALL，保持 PIO/SM/DMA
  资源不增；FIFO 为空或两次 count 相同不能单独排除在途 DMA 写入，CPU 观察不能
  冒充边沿锁存。先证明首帧边界、积压与丢字排除，再考虑坐标/ordinal 关联；
  未唯一的区间明确保留 unresolved，不授予物理身份。该报告是待实现设计，
  不代表新增原型、资源预算或门禁已经验收。

### VDC-PROGRESS-20260915-009 — READY 有界事件候选关联与原生记录

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-event-candidate/`。以下容量、数量、时间、
  代际和资源均为本轮快照，非事实源；仅验收诊断候选的软件、目标资源和普通模式路径。
- 实现：TDMA physical owner 在私有 DMA copy 交接时 pin observer epoch；RX station
  在既有 current/age/config/map/timestamp 及 Core0 decode 门通过后，于 READY
  release 前查询一次。仅凭据 flags 恰为 `TDMA_RX_CAPTURE_PRIVATE_COPY` 才进入
  pin/query；有凭据但 pin 为零时明确拒绝，READY 不借用最新 observer epoch。
  普通 origin 回传也可能具有私有凭据，并报告 UNSUPPORTED；自主 origin 和 legacy
  无凭据路径跳过。查询返回 void，不改变 RX 结果或原有时间戳/DPLL 准入。
- 失效边界：event start 绑定 DMA ARM，STOP/prepare/INVALID 退休；同时复验
  capture ID、DMA observation epoch、observer pin、独立 history.active 和原始
  DMA copy 范围。非零 bit shift 必须计入额外 word，精确覆盖边界拒绝。最多查询
  `TDMA_EVENT_HISTORY_CAPACITY` 项，不新增 MMIO、FIFO harvest 或等待；BUILDING
  取消仍等待 worker ACK，不能提前覆盖输入。
- 候选通过现有版本化双缓冲交给 Core0；ACTIVE query 仅置 dirty，随下一次已有
  event publish 发布。快照保留历史查询上下文，退休置 RETIRED；历史 MATCH_PRESENT
  不等于当前 live lease。event sequence 与 packet sequence 分别保留，禁止用
  packet 字段伪造 event 身份；`TIMESTAMP_VALID`、`DPLL_ELIGIBLE` 始终不置位。
- 原生记录 V3 只追加 candidate 组，保留 V1/V2 decoder。目标快照为 453 words，
  candidate 结构 176 B、序列化 172 B，bitmap 15 words，完整 sample 最大 1920 B。
  Storage 固定 16 KiB 未增加；窗口完整性以实际 terminal/长度核对，不能保证所有
  字段每槽都变化时仍装得下整个窗口。新 decoder 重解 008 四板 V2 原件逐字段一致。
- 软件：最终 `host-station-r2`、`host-service-r1` 真实调用路径通过；record/collector
  28 项、legacy schema 固定 SHA 2 项通过。`host-physical-r1` 3 项通过，包含
  七组真实 pin/query 场景、64 次 query oracle、实际 start/STOP/service/final fault、
  disabled 编译分支及 candidate 64 位字段/退休 flags 撕裂注入。SDK/MMIO facade
  不能替代实板物理身份。原 guard copy 函数体不变，仅移至公共编译分支。
- 资源：6/8 节点 A/B 全通过，净静态 RAM 增加 716 B，余量 17552/13792 B（已扣
  heap）；DATA end 为 `0x20008ff0`、BSS 为 `0x20009000`，SCRATCH_X 数据为零。
  目标 station 为 904 B、pin offset 为 28；event/physical snapshot 为 320/768 B。
  pin/query 位于 Flash，函数自身栈为 12/160 B；async/rx_ex/rx_once 为
  112/64/416 B。Core0 record_sample/app_record_snapshot/record_service 为
  3800/2984/128 B，局部数组较旧版共增 352 B；这些 SU 数字不能当完整调用链或
  实测高水位，Storage task 配置以 `app_tasks.c` 为事实源。
- 身份：`current-plan-r1.json` 绑定源码指纹
  `9d35742b80ae5acc785ebfb892c1d133b21a58f72f26efc1b30594a4691fd170` / 1090。
  build ID 沿用 `20260914184059`，六节点 package SHA 为
  `099fb3d97ba498ca0a1e4547e860a5e28f9e9344654829eba391dbf27ef684df`；归档源码、
  四份 map、目标 ABI 和原始 SU 已独立核对，不能仅凭 build ID 识别固件。
- 当前 P3 用时 184.078 s，quick passed 为 true，普通短帧 passed/closed_loop/
  realtime/diagnostic 全 true。`strict_gates_passed=false`：coarse CLK level 7
  在 NO3 执行 `SYSTem:TDMA:RING:TOPology 4,2,1` 时 timeout/Execution error，
  原因未闭合，不能简称物理时钟校准精度失败。普通原生窗口四板各 14 槽加 baseline
  完整，STOP/ACK 后 SD 保存读取 8.078 s，逐字节一致。
- 普通候选：NO2/NO3/NO4 query 增量为 678/693/706，matched 为 675/691/704，
  unavailable 为 3/2/2，stale/missing/ambiguous 均未增长。各从板记录直接捕获
  13 个不同 matched query；其余只由计数反映，不是逐包 trace。NO1 query 720 次
  均 unavailable；`candidates-p3-r1.json` 字段一致，未开放正式资格。
- 唯一自主采集 `capture-r1` 用时 29.094 s，在 TRIAL 命令返回 timeout 后提前
  STOP。NO1 admission 原件为 reason 14（`CALIBRATION_ORIGIN_ATTEMPT_OWNER_UNAVAILABLE`）、
  trial epoch 34、config/applied 63/63：owner 快照读取失败，observed_mask 为 15，
  尚未执行 OWNER 后的 cadence/model/recheck；不能把未记录的模型代际零解释为
  模型失效。拒绝路径压入 Execution error，不返回数字 epoch，主机等待数字约
  0.511 s 后显示 timeout；这不证明串口故障或准入计算耗时。handoff 为 UNAVAILABLE。四板
  autonomous_phase_count 全为零，不能声称已进入自主模式或测得自主候选匹配。
  请求各 18 槽，实际 NO1 4 槽、三从各 5 槽加 baseline，terminal reason 为 1、
  missed 为零，collection 均 false；保留提前终止，不追加择优轮次或放宽 timeout。
- 部分窗口：三从 query/matched 增量为 399/395、416/414、429/427，仍属普通模式；
  baseline 保留上一代 RETIRED 候选，随后当前查询更新，不得混为当前 ARM 的匹配。
  `analysis-r1.json` 连续性和总门为 false，diagnostic 为 true 不能代替完整窗口。
  全部 STOP/ACK 后顺序 SD 保存读取 5.828 s；四板部分原件的 UID/build/epoch/CRC
  可解码，SD 与 SRAM 逐字节一致。RUN 内只发控制命令，未查询采样，NO5 未操作。
- 时间反馈：`timing-feedback.json` 的 PEAK 按内部 total_ticks 选取，该记录外层
  耗时 NO1–NO4 为 1466.680/952.660/949.436/881.064 µs；不能称全窗外层最大值。
  本轮未自主准入，调用组合、状态与 008 不同，不能以数值下降证明候选实现收益、
  自主稳态或全表 WCET；008 的 NO2 UNAVAILABLE 保留。
- 下一 gate：保持 `VDC-TIME-002` IN PROGRESS，先沿准入 reason 14 追踪拒绝条件，
  保留 NO3 严格拓扑失败并区分是否相关；同时只读收敛 packet/event 物理身份、
  首事件与 DMA anchor、额外 word 覆盖和退休证明。后续实现必须先闭合对应门禁，
  不将 sequence 相等提升为正式时间戳。`VDC-TIME-003/004` 仍 PENDING；实际锁相
  未证明，OTA 实现及载荷池保持现状。
- 后续只读审计：`admission-next-gate-review.json` 确认准入只使用综合 owner 快照的
  foundation CRC，却同时依赖 result/registry/ring/scheduler 的可用性；scheduler
  使用一次 try_lock，争用是可达机制，但本次原件不足以确定失败于哪个子读取。
  最小修复候选为既有 intent guard 下的有界 foundation 专用读取，保留 revoke、
  所有实质准入谓词、最终 config/model 复验和 Core1 foundation 再验。物理身份还需
  独立 header 身份或硬件 event/DMA 坐标、晚启用首帧及积压排除、覆盖与 mailbox
  完整性证明；本轮只完成审计，未实现或验收这些后继路径。

### VDC-PROGRESS-20260915-008 — 私有报文携带 DMA 捕获凭据

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-capture-provenance/`。以下容量、数量、
  时间及代际为本轮快照，非事实源；本切片只完成 private-copy provenance 基础。
- 实现：TDMA physical owner 的 `tdma_pio_spi_phys_rx_ex()` 同调用交付私有 packet
  与 `tdma_rx_capture_t`，记录独立 ARM/capture ID、本次 observation epoch、
  candidate、复制前后 produced、frame words、bit shift 及 persona。两个 ID 在
  本 boot 内不因物理对象初始化或重臂重复，饱和后只停止诊断资格；失败、旧 backend
  及自主 origin 不生成凭据。STOP/RX 重装/ARM 入口退休 ARM 有效位。
- 同次观察：沿用已有两次 DMA 水位观察和 header 复验，不新增寄存器读取。
  candidate/frame words 包括交付前剥离的物理包头；非零 bit shift 实际读取一个
  额外原始 word。`TDMA_RX_CAPTURE_PRIVATE_COPY` 只证明私有复制与 header 检查，
  不证明 transport/mailbox CRC 或物理事件身份。成功后的凭据整理使用本次已有
  局部值，不能重读水位或将后续状态冒充捕获现场。
- 交接：可选扩展 callback 只能在 STOP 修改，更换 backend 清除旧扩展。只有
  IDLE station 接收本次 packet/capture；Core0 解析私有副本并保留凭据。REQUESTED/
  BUILDING/READY 取消沿用 ACK 所有权规则，不提前覆盖 worker 字段。字段容纳于
  原 station/注入队列 union；本切片未查询事件历史，也未产生诊断候选 lease。
- 软件：`host-station-r2` 真实 adapter 路径通过，覆盖同取消代际下连续包 ID 不同、
  worker 暂停、副本不变、各阶段 STOP、脏输出清零和 backend 切换。初轮缺少
  `assert.h` 的编译失败保留。`host-physical-r3` 共 34 项通过，六组专项执行真实
  RX ARM/dispatcher/rx_ex/旧 wrapper，覆盖偏移、反向、ring wrap、覆盖/代际变化、
  容量失败、连续包、重臂及 ID 饱和。SDK/latch/autonomous facade 不证明物理边沿。
  event adapter 回归通过；event service 初轮 fixture 缺新成员，补公共结构后通过。
- RAM 收敛：r1 热段增加导致 BSS 跨过既有对齐边界，6/8 节点余量降至
  14172/10412 B。保留原件及 r1 实板 P3；r2 仅构建，六节点仍跨界，未部署。
  最终 r3 将成功后的整理移至 Flash helper，仅扫描控制函数使用 `Os`，保留 SRAM
  复制叶；删除由 dispatcher 保证的内部重复清零，公开失败清理不变。四份目标
  map 恢复原 BSS 起点，余量为 18268/14508 B（已扣 heap 预留），净增静态 RAM
  16 B，恢复 r1 的 4096 B；SCRATCH_X 数据为零。热段为 1040 B，目标函数自身
  栈 async/helper/rx_ex/rx_once 为 112/16/64/416 B，不能当全调用链高水位。
- 当前构建：`current-plan-r3.json` 绑定指纹
  `74c67aaa7de5c5597aa027ebab9b915498268f22e3032d6d86b450f39ede66fc` / 1088，
  6/8 节点 A/B 均通过。build ID 沿用 `20260914184059`，六节点 package SHA 为
  `d15405355b43f9b364fa01cbb98b991e819da4ff6ab06841887422f79cdf5188`；各版本
  源码、构建、反汇编及 SU 归档分开保存，不能用 r1 凭证放行 r3。
- 当前 r3 P3 用时 178.265 s，普通短帧 passed/closed_loop/realtime/diagnostic
  均为 true，`strict_gates_passed=true`、diagnostic failures 为空。四板各 14 槽
  加 baseline，STOP/ACK 后顺序 SD 保存读取 6.907 s，与 SRAM 原件一致。
- 预声明唯一自主 `capture-r1` 用时 39.422 s，准入 ACCEPTED，trial epoch 为
  36、初末配置为 70、初末模型为 24。四板各 18 槽加 baseline，collection 全通过；
  三从 epoch 为 2，published 为 7167/7239/7294，无 INVALID，最大服务间隔为
  1588/1588/1581 µs，RX/TX FIFO 最大均为 4 words。STOP/ACK 后 SD 保存读取
  7.406 s，四板与 SRAM 逐字节一致；RUN 内未用 SCPI 查询采样。
- 失败保留：自主 passed/closed_loop/realtime 仍为 false，第三个健康样本最晚
  2.016150 s，超过预声明 2 s startup 门；NO1 仍有 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch`、`periodic_interval_gate_failed`。diagnostic
  为 true 只表示原生记录和从板观察连续性。未追加轮次，未追认旧失败。
- 时间反馈：`timing-feedback.json` 按原始 STOP peak 对照前切片。PEAK 由内部
  `total_ticks` 选取；NO1 该记录的外层完整相位耗时为 1484.596 µs，并非已证明的
  全窗外层最大值，且发生在 trial 前状态迁移，不能称自主稳态；NO3/NO4 对应耗时
  为 1043.324/1032.832 µs，NO2 原响应 UNAVAILABLE。可用记录低于前轮同板记录，
  但调用组合和 cache 状态不同，不能据此证明 `Os`/helper 的独立收益、完整 WCET
  或全窗预算通过；缺失项不以旧值补齐。
- 下一 gate：继续 `VDC-TIME-002`，在 Core1 READY 边界验证捕获仍属于当前 ARM/
  observer 代际后执行有界 history 候选查询；显式处理覆盖、迟到、错序和 STOP/
  INVALID 退休。同 sequence 只作候选，FLIGHT_MUTABLE 头 CRC 不替代所有邮箱
  CRC；物理 anchor 与身份仍待证明。原生记录尚未导出逐包凭据字段，实板结果只
  证明本切片资源及原 TDMA/observer 连续性，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-007 — 临时准入原因与失败采集留证

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-admission-diagnostic/`。以下数量、
  时间、容量及代际是本轮快照，非事实源。正式时间戳和 DPLL 资格未改变。
- 实现：Calibration 的串行 Core0 控制路径记录最后一次已进入准入函数的尝试，
  按实际短路分支保存 reason、请求参数、已观察字段 mask、初末配置/模型代际及
  reason 对应的 observed/expected。`READ:CALibration:ORIGin:DIAGnostic?` 仅在
  STOP/ACK 后读取；helper 不可得只报告该层不可得，不用事后快照猜内部原因。
  STOP/REVOKE 保留历史；ACCEPTED 和 attempt 计数均不授予当前权限。
  SCPI 参数解析失败发生在准入函数之前，仍维持旧行为，不创建尝试或撤销旧 grant。
- 语义验证：`host-core-r1` 的三组真实 C/SCPI 测试通过，其中 47 个场景与
  `5799b7f` 原函数逐字冻结的 oracle 对照，返回、grant 字段、epoch、helper
  读取顺序及次数一致；覆盖所有短路 reason、代际、mask、计数饱和、64 位输出、
  REVOKE 历史保留及 STOP/ACK 查询门。`host-existing-r2` 共 26 项通过，含
  16 项既有准入和 10 项 collector 编排测试。后者使用 facade，不证明真实串口
  或原生记录 CRC；当前实板负测和原生导出另行提供证据。
- 工具：`tdma_autonomous_record.py` 单次申请有限许可证，RUN 内只发控制命令；
  拒绝、非法代际、部分 START 或导出失败均保留为错误。异常后仍尝试全部 STOP
  和 REVOKE，全部 STOP/ACK 成立才导出 SRAM；一个板导出失败不丢弃其余板，
  后续可选诊断查询失败不丢弃已导出的原件。失败最终仍抛出，不自动重试或择优。
- 参数勘误：此前 `VDC-PROGRESS-20260915-005` 及旧采集计划将 8192 标为 events/
  `grant_event_limit` 的描述有误。真实 `calibration_manager_origin_trial_configured()`
  第二参数是 `rearm_budget_ticks`；本轮请求为 8192 clk_sys ticks、256 次 abort
  polls、30 s 期限（250 MHz 下为 7500000000 ticks），未证明任何事件数量配额。
  旧封存原件不改写，本轮 `acquisition-plan.json` 明确纠正口径。
- 资源与构建：`current-plan-r1.json` 绑定源码指纹
  `116d29292bb64076e94f2691095c8490d26d4569ff4865400fe1c4ccacf39f0c` / 1087，
  6/8 节点 A/B 均链接通过。build ID 沿用 `20260914184059`，必须同时核对
  package SHA，六节点为 `c91efb6172c9097e52bd6b1706a676f353873c2c14b6e8fe481871355abb86e3`。
  目标 map 的 `s_origin_attempt` 为 80 B，扣 heap 预留后的余量为
  18284/14524 B，SCRATCH_X 数据为零；无新增 Core1 诊断消费路径。
- 当前源码 P3 用时 184.562 s，普通短帧 passed/closed_loop/realtime/diagnostic
  全为 true，`strict_gates_passed=true`、diagnostic failures 为空；本轮安全 SCK
  行为 `[1,0,0,0]`、最小 margin 为零。四板各 14 槽加 baseline，STOP/ACK 后
  顺序 SD 保存读取 7.766 s，CRC/UID/build/epoch 与 SRAM 原件一致。新一轮通过
  不能追认此前 coarse/SCK 拒绝已通过，也不能据此归因旧 grant 失败。
- 预声明负测 `negative-stopped-r1`：四板停稳后唯一 TRIAL 返回 `<timeout>`，
  ERR 为 -200；reason 为 `RING_DISABLED`，mask 仅含 ring，observed/expected
  为 0/1，请求值完整。REVOKE 前后历史逐字段一致，live grant enabled 为零。
  这直接证明拒绝可表现为主机无数值响应，不能据此推断执行耗时超预算。
- 预声明唯一自主 `capture-r1` 用时 39.969 s，准入 ACCEPTED，trial epoch 为
  40、配置初末均为 71、模型初末均为 24，mask 为 1023。四板各 18 槽加 baseline，
  collection 全通过；三从 epoch 为 2，published 为 7120/7185/7253，完整记录内
  无 INVALID，最大服务间隔为 1579/1585/1588 µs，FIFO 最大为 4/4/2 words。
  顺序 SD 保存读取 8.766 s，与 SRAM 逐字节一致。本轮没有发生 grant 拒绝，
  `VDC-PROGRESS-20260915-006` 的具体拒绝原因仍 unknown。
- 自主总门禁保持 false：第三个连续健康样本最晚在约 2.012 s 完成，超过预声明
  2 s startup 门；NO1 另有 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch` 和 `periodic_interval_gate_failed`。
  diagnostic 为 true，只说明本次完整记录与从板观察连续性，不能提升为全部自主
  门禁、真实输出锁相或物理时间精度通过；不追加择优轮次，不放宽期限。
- 下一 gate：准入诊断完成后继续 `VDC-TIME-002`，先将真实 DMA 复制前后水位、
  observation epoch、候选范围/bit shift 与跨 ARM 的独立代际随私有 packet 交给
  RX station；独立验收后再做 Core1 READY 边界的原始事件候选查询。station 取消
  epoch 不作逐包 ID，FLIGHT_MUTABLE 的头 CRC 不等于所有邮箱 CRC 通过；
  同 sequence 仅是候选，物理身份与 anchor 仍待证明。`VDC-TIME-003/004` 保持
  PENDING，OTA 实现和 NO5 保持本轮既定范围。

### VDC-PROGRESS-20260915-006 — 原始事件有界历史与失效退休

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。承接已封存的
  启动准入切片，先补齐身份关联需要的原始事件保留；此切片尚未接入 packet 查询、
  逐 capture lease 或 DPLL 输入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-history/`。以下构建、容量、
  数量及时间是本轮快照，非事实源；深度以 `TDMA_EVENT_HISTORY_CAPACITY` 为准。
- 实现边界：原先每批只将末项写入快照，新历史由 TDMA Core1 physical owner 独占，
  在整批 observer 校验及最终 sticky-fault 复验之后接收完整批次；分别检查源 joined
  数、epoch、ordinal、sequence、原始时间和诊断 flags，失败不留下部分新记录。
  容器接受正确的 sequence 回绕，生产 observer 仍在 sequence 上限拒绝，不能据此
  宣称生产跨回绕连续性；ordinal 不得回绕。STOP、重新准备和 INVALID 退休
  历史，旧 epoch 查询和已淘汰条目不使用 latest 回退。
- 时间/容量边界：compact 条目只保存 RX/TX elapsed、raw counter、sequence 与
  ordinal；context 保存保守初始 start 区间，查询不会将逐事件收窄的 anchor 伪装为
  同一精确时间点。固定保留深度只限制资源与工作量，尚未证明覆盖所有准入配置的
  最坏 DMA/捕获/解析等待；复制出的诊断条目也不是跨 STOP/INVALID 有效的 lease。
- 验证边界：本切片沿用板端记录 schema，实板验证只判断静态资源与原观察器/TDMA
  运输连续性；逐项历史查询由执行真实生产函数的 host 测试验证。完整报文身份、物理 anchor、
  timestamp_valid/dpll_eligible 均未开放，`VDC-TIME-003/004` 继续 PENDING。
- 软件验证：`host-core-r1` 运行完整容器实现，共 11 组 C 用例，覆盖多批次淘汰、
  非法尾项整体退休、丢失/截断批次、旧 epoch、重复候选、时间溢出及 ordinal 边界；
  `host-integration-r1` 的 adapter/service/observer 共 4 项 pytest 通过。真实
  owner→phys→observer 路径在 RX 等待/队列早退时逐项查询整个保留窗，验证每批
  非末条记录；feed 后的最终 fault 注入使旧记录和新批次全部不可查询。独立
  source/host 审核结论为 `PASS_LIMITED_TO_DIAGNOSTIC_EVENT_HISTORY_BASE`。
- 当前源码/资源：`current-plan-r1.json` 指纹为
  `f3e9b0296c3904018b11ce640117026f812344286a21c3aa5bff7874dfff23a9`，6/8 节点
  A/B 构建均通过，build ID 为 `20260914184059`，同时绑定各 package SHA。
  目标 nm 的 history 对象为 568 B，条目为 512 B，其余是 context/对齐；两容量
  静态 RAM 增量均为 568 B，扣 heap 预留后的余量分别为 18364/14604 B，
  SCRATCH_X 数据为零。主机 sizeof 为 576 B，目标枚举 ABI 为 small，不混用。
  `target-abi-r1.json` 复用目标实际编译参数，函数自身静态栈为 append 96 B、
  lookup 40 B；不等于 Core1 调用链栈高水位，lookup 本轮尚未接入生产消费路径。
- 当前源码 P3：`p3-run-r1` 耗时 182.359 s，普通短帧 passed/closed_loop/realtime/
  diagnostic 均为 true，四板各 14 个定时槽加 baseline 完整，STOP/ACK 后顺序
  SD 保存读取耗时 8.547 s，SD/SRAM 一致。三从 epoch 为 1，published 为
  674/689/703，最大服务间隔为 1588/1583/1577 µs，FIFO 为 2/2/1 words。
  本轮 coarse/coded 校准通过，但 `strict_gates_passed=false`：TRN-01 SCK 的
  link 1 候选覆盖失败，TRN-03 没有满足 flight re-arm budget 的实测 SCK 行，
  选择行 `[1,0,1,0]` 的最小从板 margin 为 -1 sample。原件留存，不能提升为严格
  校准或物理时间精度通过；前一切片的 coarse NO2 失败仍保留于其封存目录。
- 自主对照失败：预声明的唯一 `capture-r1` 在四板 START 和 PROFILE RESET 完成后，
  `CALibration:ORIGin:TRIAL` 返回 `<timeout>`；工具将其转整数时异常，28.328 s
  结束并执行全部 STOP/REVOKE，没有完成自主窗口。`capture-r1-recovery` 救回
  epoch `1789411695` 的 SRAM 原件，四板仅 4/5/5/5 个定时槽加 baseline，已写槽
  CRC/身份均正确，但 terminal reason 为 STOP、collection 全为 false；随后顺序
  SD 保存读取 6.656 s，SD/SRAM 完全一致，不将救援成功改成采集成功。
- 拒绝证据：恢复时首先读到 `-200 Execution error`；正确的 STOP 后
  `READ:CALibration:ORIGin?` 显示 version/trial_id/enabled 均为零，HANDoff 为
  UNAVAILABLE。SCPI 准入回调在返回 ERR 时不输出数值，因此 `<timeout>` 不能证明
  命令执行超过预算。具体拒绝分支尚未记录；该准入链不直接读取 SCK replay-safe
  字段，不能仅凭本轮 SCK 失败归因。救援中误发的 `CALibration:ORIGin?` 缺少 READ
  前缀，错误命令原件单独保留，不能用作 grant 证据。
- 有限结论：source/host、目标资源和普通短帧证据闭合，独立审核允许仅按
  `PASS_LIMITED_TO_DIAGNOSTIC_EVENT_HISTORY_BASE` 提交；自主补测未闭合，不追加
  择优轮次。已有从板 ACTIVE 样本来自授权失败前的普通发车，不能代表自主连续性。
- 下一 gate：先为 grant 准入建立可观察的拒绝 reason/阶段，再按预声明计划复测
  完整自主窗口；保持既有授权条件及有限许可证，不以增加 timeout 或忽略 ERR 放行。
  随后逐 capture 唯一编号随真实 DMA observation epoch、复制范围和复制后
  复验结果进入 RX station，捕获时保留所需候选，Core0 只解析 station 私有 packet，
  Core1 在 READY 后复验配置/map/取消代际及龄期，再做诊断关联。不能把
  RX_PREPARE 的 STOP 取消 epoch 当逐 packet token，也不能仅以 sequence 相同给
  最新事件贴上调用者提供的 expected identity；同序列异配置、覆盖、错序、重臂与
  迟到分别拒绝。后续物理 anchor 与正式资格另行验收。

### VDC-PROGRESS-20260915-005 — 最终启用前 CS 准入复验

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。承接上一
  checkpoint 的 NO3 启用前双 CS 已低反例，不改变时间戳或 DPLL 资格。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-start-gate/`。
  以下构建、数量、容量和时间均为本轮快照，非事实源；旧失败仍保留于上一切片。
- 实现：`tdma_event_start` 在配置/seed 完成后的最终 pad-before 快照上复验双 CS；
  任一为低时仅发布已有等待/pad 状态并返回，观察 SM 保持关闭，不递增 epoch。
  下个已有 TDMA owner 相位可以重试，每次工作有界，没有内部轮询或新 PIO/DMA。
  这不承诺有限的总启动等待时间；既有有限流程/许可证负责结束未启动的观察。
- 拒绝边界：最终检查至 enable 之间仍可能遇到边沿，启用后原 DIRTY_START 检查保留，
  INVALID 不因电平恢复而复活，必须显式 STOP/ARM。等待状态继续由 Core1 唯一写者
  经版本化双缓冲发布，不授予正式 timestamp 或完整 packet identity。
- 软件/资源：adapter pytest 通过，执行真实启动、退休、快照和读取函数；使用板级
  RX/TX CS 宏注入单 RX 低、单 TX 低、双低。每组连续三次延后均不启用、不推进
  epoch 或 enable 计时区间，SM0 不变；恢复高后只启用一次。三种启用后低组合均
  永久拒绝，显式退休和重新准备后才进入新 epoch。既有跨核复制/IRQ/退休覆盖保留。
  独立 source/host 审核通过；6/8 节点 A/B 构建通过，静态 RAM 增量为零。
- 当前源码：`current-plan-r1.json` 绑定源码指纹
  `b33ccea73ae8a10b1f62f4965221b9a44d7fd38e55e8d44ac934b3f90448b45f`；增量构建
  沿用 build ID，身份同时核验 package SHA、OTA CRC 与源码指纹。容量 6/8 的 A/B
  扣除 heap 预留后静态余量分别为 18932/15172 B，SCRATCH_X 数据占用为零。
- 当前源码 P3：`p3-run-r1.json` 耗时 181.250 s，quick diagnostic 凭证通过，普通
  短帧 passed、closed_loop、realtime、diagnostic 均为 true；四板各 14 个定时槽加
  baseline 完整，SD/SRAM 一致。凭证的 `strict_gates_passed=false`：coarse CLK
  level7 中 NO2 的 `TOPology 4,1,3` 超时及 `-200 Execution error` 原件保留，不能
  将后续普通短帧通过描述为全部严格校准通过。
- 固定三轮自主观察：按 `acquisition-plan.json` 完成 `capture-r1/r2/r3`，不追加择优
  轮次。wire 周期 1 ms、Core1 整表 1.5 ms、startup 2 s 加运行窗口 6 s、状态快照
  500 ms、有限 grant 30 s/8192 rearm ticks（原 events 口径勘误见 007）；每轮四板均为 18 个定时槽加 baseline，
  collection 为 COMPLETE、无漏采，全部 STOP/ACK 后顺序保存 SD，与 SRAM 逐字节
  一致。快照间隔沿用上一轮容量修正，不能等同于原 250 ms 记录密度。
- 三从连续性：各轮 observer epoch 依次为 2/3/4，从首次 ACTIVE 至末样本均为
  ACTIVE/fault0/FDEBUG0，FIFO 高水位均为 4/4/2 words。NO2/NO3/NO4 的 published
  依次为 7117/7191/7269、7169/7239/7316、7142/7205/7267；最大服务间隔分别为
  1590/1583/1592、1591/1594/1626、1600/1588/1593 µs。采集耗时分别为
  42.688/39.281/39.375 s，STOP 后 SD 保存读取为 7.375/7.297/7.390 s。
- 结论边界：三轮 `diagnostic_observer_continuity=true`；原工具 passed、closed_loop、
  realtime 仍为 false，diagnostic 为 true，startup 与主板自主 persona/software TX
  条件失败保留。板端快照没有捕获 waiting 且最终低 CS 的短暂状态，延后分支只具有
  host 注入覆盖，不能声称实板直接命中或任意启动均可成功，也未证明物理首事件精度。
- 下一 gate：启动准入切片验收后继续 `VDC-TIME-002` 的原始事件与已校验 packet
  身份候选关联，显式绑定 observer/ARM 代际、逐 capture lease、DMA 复制范围和
  取消退休；拒绝覆盖、同序列异身份和旧会话。随后补物理时钟 anchor；诊断候选不
  授予 timestamp_valid/dpll_eligible，`VDC-TIME-003/004` 继续 PENDING。

### VDC-PROGRESS-20260915-004 — 自主事件 FIFO 与解析交接解耦

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。普通模式
  验收之后继续自主发车诊断；不开放正式时间戳、共同时间映射或锁相准入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-autonomous/`。
  以下数量和时间均为本轮快照，非事实源。`preflight-r1.json` 核对原普通模式
  固件包、源码指纹、四板 UID/build 与 STOP/ACK；失败原件单独保留。
- 修复前：`capture-r1` 采集耗时 40.312 s，STOP/ACK 后顺序 SD 保存与读取耗时
  9.875 s。每板 34 个定时槽加一条 baseline 全部有效，无漏采；CRC、build、UID、
  epoch 和 SD/SRAM 原件一致。记录完整不代表事件有效。
- 失效证据：三从在定时 slot 8 首次报告 `TDMA_EVENT_PRE_FAULT`，fault 为
  `TDMA_EVENT_FAULT_STALL`，FDEBUG 指向两个 counter SM 的 RXSTALL；RX/TX FIFO
  高水位各 8 words，sequence 为 5 words。NO2/NO3/NO4 发布计数停在
  395/638/521，最大服务间隔 4479/4500/4542 µs，此后保持 INVALID。
  失效前相邻快照的 elapsed/ordinal 增量给出约 1000.3 µs 的自主事件间隔，
  与矩阵的 wire 周期相符；Core1 整表周期是另一配置，不能混为发车周期。
- 定位：原 `tdma_pio_spi_phys_event_service` 仅从 capture 路径调用，而
  `tdma_pio_spi_ring_adapter_rx_once_impl` 在 RX_PREPARE 未退休时提前返回，导致观察 FIFO
  等待 Core0 解析交接。固定 FIFO 可容纳的完整事件数量由
  `TDMA_EVENT_FIFO_WORDS` 与成对 counter 输出决定；本轮五事件积压已超过该容量。
- 门禁边界：修复前工具的 diagnostic_passed 为 true，但 passed、closed_loop 和
  realtime 均为 false；主板还有自主 persona 不匹配和一次停止/恢复观察，必须保留，
  不能全部归因于从板 observer。三从原有 TDMA 节点门禁通过，事件失效后只停观察。
- 修复：在 `tdma_pio_spi_phys_service_tx` 的 origin 条件早退前服务观察 FIFO，
  删除 capture 中的旧调用。既有 `tdma_runtime_owner_service_phys_tx` 每个获准执行
  的 TDMA 相位先处理生命周期，再进行有界物理服务；RX_PREPARE 繁忙不再阻挡观察。
  原 OTA/显式跳步、armed/persona、epoch 和 fault 条件保留，不增加 PIO/DMA。
- 软件/资源：四个 observer 测试模块共 6 项通过。新增测试执行真实 component、
  runtime owner、物理服务及 consumer 调用链，在 24 个模拟相位中覆盖 RX 准备各
  非 IDLE 状态、队列早退、无待发 TX、capture 不重复、未 ARM/错误 persona/未初始化
  及 FIFO 堵塞永久拒绝。寄存器和无关 owner 边界仍为 stub，不证明实板时序。
  独立源码/host 审核通过；容量 6/8 的 A/B 构建通过，静态 RAM 增量为零，
  记录 schema 和 PIO 程序未变。当前源码/固件身份见 `current-plan-r1.json`。
- 当前源码 P3：`p3-r1` 完整流程 180.407 s，普通短帧 passed、closed_loop、realtime、
  diagnostic 全部通过；每板 14 个定时槽加 baseline 完整有效，三从最大服务间隔
  降到 1575/1590/1580 µs。四板 STOP/ACK 后顺序保存 SD，与 SRAM 原件逐字节一致。
- 自主复测的容量失败：`capture-r2` 中三从从首次 ACTIVE 至最后已记录 slot 30
  持续无 fault，FIFO 高水位为 4/4/2 words；但原生记录只写入 31/34 槽，
  `TDMA_RECORD_OVERFLOW` 终止，不能声称整窗通过。事件持续变化增加 delta 文件
  长度，耗尽 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES`；不是 STOP 或 FIFO 堵塞。
  工具泛化的 startup timeout 在本轮实际由 collection_errors 导致，独立原因见
  `recorder-capacity-correction-r2.json`。因此保持运行窗口和逐事件验证不变，后两轮
  仅将状态快照间隔改为 500 ms；快照分辨率与区间门禁密度减半，不等价于原采样密度。
- 启动拒绝：`capture-r3` 四板均完成 18 槽，但 NO3 在启动时报告
  `TDMA_EVENT_FAULT_DIRTY_START`，未发布事件。最终 enable 前后的 pad 快照中
  RX/TX CS 都已为低，两次快照间只有 RX DATA 改变；不能描述为该区间内 CS 才下降。
  原早期 CS 检查在配置/seed 之前，最后 pad-before 后没有阻止 enable 的准入复验。
  故障拒绝有效，启动竞态未修；NO2/NO4 在完整记录中保持 ACTIVE，所有原件保留。
- 显式重臂：`capture-r4` 用相同固件/配置重新 STOP/ARM，三从进入新 epoch，四板
  18 个定时槽加 baseline 全部有效、无漏采、reason 为 COMPLETE。NO2/NO3/NO4
  启动等待后至窗末均 ACTIVE、fault 为零，发布 7094/7185/7258 条事件；最大服务
  间隔 1640/1590/1592 µs，最大服务耗时 301/312/304 µs，FIFO 高水位为
  4/4/2 words。采集与导出 39.625 s，STOP/ACK 后顺序 SD 保存及读取 7.828 s，
  CRC/身份/SD-SRAM 字节一致。该窗口支持本配置成功启动后的 FIFO 服务容量，
  不消除上一轮启动失败，也不是更长周期配置或任意调度阻塞的证明。
- 验收边界：`analysis-r4.json` 的诊断事件连续性通过，原工具 passed、closed_loop、
  realtime 仍为 false。startup 的三次连续稳定观察受启动填充、自主切换及采样间隔
  影响；主板软件 TX 计数与普通 persona 检查也不适配自主模式。保留这些失败，不修改
  gate 来授予完整自主验收，更不授予 `timestamp_valid`、`dpll_eligible` 或锁相。
- 下一 gate：服务与解析解耦切片完成，继续 `VDC-TIME-002` 的最终 enable 前 CS
  准入复验及有界等待，保留 enable 后 DIRTY_START 拒绝和显式重臂要求；随后补齐
  物理首事件、完整 identity 和时钟 anchor。`VDC-TIME-003/004` 继续 PENDING。

### VDC-PROGRESS-20260915-003 — 成对事件观察器生产接入与原生记录

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。RAM 使能验收
  后返回时间输入主线；本切片仅发布诊断数据，不开放正式时间戳或锁相准入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-production/`。以下构建、
  数量、容量和时间均为本轮快照，非事实源；第一次失败及修复前源码/固件单独保留。
- 实现：`PROJECT_TDMA_EVENT_OBSERVER` 选择 follower 的成对计数与序号采样程序，
  真实汇编模板与已审核原型一致；控制与观察共用原 TX PIO 的固定布局，不新增 DMA。
  纯 C consumer 每次最多接收 `TDMA_EVENT_STREAMS * TDMA_EVENT_FIFO_WORDS` 个字、
  输出 `TDMA_EVENT_MAX_RECORDS` 条诊断记录。半对等待、共同 epoch、多次回绕唯一
  提升、绝对年龄约束和发布前故障复验均在 TDMA owner 内完成；失效只停观察 SM。
- 生命周期与交接：模式切换及 SM 复位前退休 pending 和旧 epoch；旧 TX latch 不再
  消费新的成对 FIFO。事件状态由 Core1 私有工作区生成，经版本化双缓冲交给 Core0；
  复用旧槽前 DMB、提交时 release，读取有界复验版本及复制时限。原生记录 schema
  升级后仍可解码旧版封存记录，文件保存继续位于全部 STOP/ACK 后。
- 软件证据：核心/记录器 19 项、loader/PIO 16 项、adapter 1 项通过。包含生产 C
  生命周期、3440 组 bigint 时间提升对照、真实 pioasm 安装/冲突回滚、双向切槽、
  发布前读取旧完整快照、旧槽复用/撕裂拒绝及 requested role 改变后的退休。独立
  审核结论仅为诊断源码与 host 范围通过；PIO stub 不证明真实采样相位。
- 首轮失败：quick P3 流程耗时 190.484 s，普通模式环路节点在 soak 中均健康，三块
  从板事件发布增长且无事件 fault；NO2 一个原生样本 `valid_mask=0x37` 使整窗采集和
  启动 gate 失败。该位图只能定位为物理快照不可用，未区分 odd/changing guard 与
  复制期限分支；双缓冲修复消除单槽写入窗口，没有放宽读取复验。四板 SD 与 SRAM
  原件一致。第二轮更新固件后，NO2 TOPOLOGY 未确认使 MARK 校准准备失败，原件保留。
- 双缓冲复测：复用已核验固件/OTA 的校准验收耗时 77.579 s，不含编译与传输；NO3
  又有一个物理快照不可用样本，整窗 gate 仍失败。完整 snapshot 耗时 2297 µs，不是
  event copy 独立耗时，也不能据此认定唯一失败分支。随后仅为读取的固定次数复制及
  复验屏蔽调用核中断，统一恢复原 PRIMASK，避免 RTOS 抢占消耗复制期限；未引入
  跨核等待。该结果期限不是关中断 WCET，实际时长仍须独立测量。
- 当前普通模式验收：`p3-r4` 对应源码指纹见 `current-plan-r3.json`，四板真实更新后
  quick P3 用时 186.937 s，短帧 passed/closed_loop/realtime 与诊断门禁均通过。
  每板原生记录 14 个采样槽均有效、无漏采；NO2/NO3/NO4 发布 674/690/703 条事件，
  启动等待后至窗末均保持 ACTIVE 且 fault 为零，RX/TX FIFO 高水位均为两个字，
  sequence 为一个字；主板不启用此 follower 观察器。
  四板 STOP/ACK 后顺序 SD 保存，`p3-sd-r4.json` 核对全部文件与 SRAM 原件一致。
- 资源/耗时：最终容量 6/8 的 A/B 链接均通过，扣除链接器 `.heap` 保留后的静态 RAM
  余量为 18932/15172 B，
  相对 RAM 回收切片新增 1820 B，OTA 未改。从板本轮 service 最大耗时为
  255/240/275 µs，服务最大间隔为 4562/4515/4586 µs；这些包含启动/调度影响的
  诊断最大值不能当作自主发车下的容量证明或新增阶段的独立 WCET。
- 下一 gate：继续核对当前固件短帧闭环及自主发车下的 FIFO 容量/服务间隔，随后补
  物理首事件、CS 相对前缀、完整 packet identity 与物理时钟 anchor。
  `timestamp_valid`、`dpll_eligible` 继续为 false；`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-002 — 成对事件计数与有界联合读取原型

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS；RAM 切片
  验收提交后已返回时间输入主线。本条完成离线原型，不是生产加载或板端锁相验收。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-epoch-prototype/`。以下布局、
  容量、案例数及公式参数均为原型与刺激快照，非事实源；旧失败原件保留。
- 联合读取：`epoch_model.py` 建模共同启动条件、固定容量 staging、前后及提交前
  状态复验、部分记录等待、有界超时、永久 INVALID、STOP 退休 ACK 与新代际。
  运行中不清 rising IRQ；只停止观察 SM。旧代际输出在失效/STOP 后不可重新使用。
  粗时间区间筛选回卷候选必须恰好唯一；累计时间还须与当前 FIFO 年龄区间及同一
  启动区间相交。过期记录在消费前拒绝，不能由晚到 packet 复活。
- 原方案反例：独立复核发现 RX/TX 同时少一条时，相对时差仍正常，旧三路按下标
  联合会把后续 raw 配给前序 sequence，之后才因超时失效。真实执行反例见
  `unpaired-both-raw-loss-counterexample.json`。因此未将旧模型测试通过当作身份
  绑定通过，也不靠放宽时差门禁接入 DPLL。
- 新候选：同一个 counter SM 通过两次 `IN ...,32` autopush 输出 X 原始倒计时及
  Y 事件倒计数，再递减 Y。sequence 每帧至少移入完整序号位数，因此可去掉冗余
  ISR 清零。`paired-placement.json` 实际汇编原控制与新程序：控制 10 字、共享
  counter 10 字、sequence 12 字，固定起点 0/10/20，总计 32 字；仍在 follower
  TX PIO 的原四个 SM 内，未新增 DMA、未借 PIO0/DMA7。
- 模型验证：`r1-paired-result.json` 的 159 个案例通过，覆盖机器码相位/前缀、X
  回卷、序号采完前截断、机器码模型中的 autopush stall、单双路完整记录遗漏、单 word
  遗漏、半对等待及 STOP 交错。硬件 Y 值必须按共同 seed 连续递减，再与 wire
  sequence 联合；仅比较 RX 的 Y 等于 TX 的 Y 仍不足。修复前失败、旧模型的
  阻断反例与最终候选结果分别保留，不能混算为生产准入。
- 时间/容量变化：新 raw 在检测后一个模型周期输出；相邻完整事件使用
  `2*d + 5 + w0 + q*(2*M+1)`，首点开销为一个周期。其中 `M=2^32`，`d` 是 raw
  模差，`w0` 来自 raw 大小关系；q 仅由粗时间区间唯一选择，不读模型 wrap 真值。
  同时 raw FIFO 的八 words 仅容四个完整事件；半对是正常瞬态，不直接当作故障。
- 边界与下一 gate：Y 只计本 SM 检测到的事件，尚未证明全部物理 CS 均被捕获。
  仍须证明当前自主发车的序号唯一性、完整帧/身份候选来源、共同初始化、CS/SCK
  和 DATA 相位、RX/TX 转发时差及四事件容量下的最坏服务间隔，再做目标 C/loader
  与 STOP 路径、链接/P3、四板原始计时和实际物理 anchor。所有原型输出的
  IDENTITY_BOUND、有效时间戳及 DPLL eligible 继续关闭；ARM 粗区间不取中点冒充
  pad 时刻。`VDC-TIME-002` 未关闭，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-001 — 为时间输入调试回收共享采样区 RAM

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；资源实施由 SYNC_IO 的
  `SYNC-RAM-001` 承接，详见 `SYNC-PROGRESS-20260915-001`。
- 状态：DONE（RAM 使能切片）。用户选择最大收益的共享采样区缩容，明确保持 OTA
  实现不变。容量派生、owner 租约、行为回归、当前源码四板 quick P3、STOP 后 SD
  原生记录一致性和四板各两轮 RAW 重臂均通过；主线 `VDC-TIME-002` 仍在推进。
- 证据：`out/HardwareAcceptance/20260914/dpll-ram-arena16/` 保留软件验证与失败；
  `out/HardwareAcceptance/20260915/dpll-ram-arena16/` 保留最终构建和硬件验收。
- 边界：回收静态余量用于继续 TDMA/DPLL/VDC 调试，不提升内部/正式锁相状态；
  不以 map 通过证明高频采样可靠性，也不借用其他 PIO 或 DMA owner。
- 下一 gate：返回 `VDC-TIME-002`，在 `VDC-PROGRESS-20260914-028` 原型上补共同 epoch、有界联合
  harvest、丢事件永久失效及 raw 时间提升；生产加载与实际时间锚仍须独立验收。

### VDC-PROGRESS-20260914-028 — 从板连续事件观察的 PIO 可执行原型

- TODO task ID：`VDC-TIME-002`、`VDC-RESOURCE-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成实际 pioasm 与机器码指令模型；未修改生产 persona、
  固件或硬件，未开放正式时间输入。模型通过不代表四板绑定或锁相通过。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-event-observer-prototype/` 的
  `placement.json`、`continuous-result.json`、`joint-gap-counterexample.json`、
  `identity-ambiguity.json` 及独立审核。以下容量、编号和时间均为源码/刺激快照，
  非事实源；PIO/RAM 生产准入仍以 owner 配置、目标链接和实板验收为准。
- 资源方向：当前 process follower 的 TX PIO 为控制转发加旧 latch，RX PIO 已满。
  原型以实际控制程序和两份新观察程序进行固定地址汇编：控制 10 字、RX/TX 共用
  连续计数 9 字、wire sequence 采集 13 字，起点分别为 0/10/19，总计 32 字。
  候选使用 TX PIO 的四个 SM；无新增 DMA、未借 PIO0/DMA7。该结果仅证明静态
  指令布局可容纳；未执行真实控制 SM、生产加载器或 GPIO/资源安装路径。
- 单次与连续边界：20 字单次 raw+sequence 原型通过 1560 组相位刺激，但采集
  DATA 时倒计时暂停，且短 CS 高脉冲可漏检，不能直接成为逐圈时间通道。6 字
  连续计数原型在低电平期间 X 回绕会重复产生 fall；实际机器码反例已保留。
  9 字版本补齐两个回绕分支，并在 CS rising 发布相对 IRQ，序号 SM 在发布前
  检查 sticky，跨帧截断进入错误分支；正常帧等待并清除对应 IRQ 后接下一帧。
- 连续模型：272 组相位/前缀/SM 顺序刺激、20 组计数回绕、64 组普通截断通过。
  无 stall/无漏事件且单次可辨回绕时，指令模型满足相邻事件间隔
  `2 × raw decrement + 4 × event delta + wrap count`。当前 wrap 数使用模型内部
  真值，尚未实现仅凭 raw 与有界 epoch 的生产重建。FIFO 满负测证明会 stall，
  未证明真实 harvest 能在前后 sticky 检查后原子提交或永久失效；对象重置仅是
  模型新 epoch，不能当作 STOP/重臂硬件退休验收。
- 新反例：联合间隔扫描共 864 组，其中 CS 高仅一个模型周期的 108 组均失败；
  可出现三路 FIFO 数量一致且无 stall，但保留的物理帧不同。后续逐条复核旧标签
  的 36 个案例：RX/sequence 保留第 1、3 帧，TX 保留第 1、2 帧；不能称为三路
  同漏中间帧。更正证据见新日期目录的 `prior-gap-label-correction.json`，旧原件
  不改写。较宽间隔在有限刺激中通过，
  不能将其最小值固化为芯片门限。真实 transport 编解码另生成相同 sequence、
  不同 schedule/identity 的两个 CRC 正确帧，序号观察无法区分；不能直接置
  IDENTITY_BOUND。每个 epoch 的来源/配置/序号唯一性必须独立证明或补采身份。
- 下一 gate：先实现三路共同 epoch 的有界 harvest、raw 回绕提升、缺样/溢出
  永久失效和显式重新锚定；证明已准入 CS 高宽、首 SCK、真实 DATA 相位与前缀，
  覆盖尾位截断、IRQ 设置/清除竞争、旧 FIFO 与 STOP/persona 退休。分辨率和
  ARM timer 区间不能代替 pad 边沿精度。只有身份策略和这些负测闭合后才进入
  当前资源链接/P3/四板原始计时诊断，继续维持 TDMA resident 与 HAOFV owner
  边界。`VDC-TIME-002` 未关闭，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260914-027 — 最新帧选择前的边沿身份反例

- TODO task ID：`VDC-TIME-002`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成当前源码的独立只读审核与可执行反例；未修改固件、
  未操作硬件或开放正式时间输入，当前板端仍为上一切片已验收版本。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-frame-edge-binding-audit/` 的
  `binding-counterexample-r2.json`、`binding-probe-r2-command.json`、基线源码及独立
  审核记录。以下编号和周期为测试刺激快照，非实板测量或物理时延事实源。
- 反例：编译实际 `tdma_rx_scan.c`、`tdma_transport_frame.c` 和从当前源码抽取的
  `tdma_pio_spi_phys_take_local_tx_edge_ex()`。真实编码/解码均通过的帧 100 与 102，
  通过真实 capture-hint 提供各自 identity；受控边沿源保留帧 100 的时间戳，调用者
  换成帧 102 后，真实 helper 仍返回新 identity 和完整 TIMESTAMP/SEQUENCE/IDENTITY
  有效标志。正例、不可用及空 context 负例也执行。该结果证明接口没有独立的
  事件身份校验，不能用随后 packet CRC 正确来证明边沿属于该 packet；不宣称已
  测出实板错配率。
- PIO 边界：指令模型核对当前 `tdma_pio_spi_flight_clock_latch` 的全部指令，
  在默认未合并 FIFO 的刺激中，第一帧低电平填满 FIFO，后续帧 PUSH noblock 丢弃，
  最早条目仍属于第一帧。RX 在取得 packet 后读取该 FIFO，adapter 再将时间戳写到
  所选 view 的 sequence/identity 下；TX helper 直接采用 expected 字段并置 BOUND。
  当前 earliest 策略也会在 clamp 后前跳，已有绑定前提同样未闭合；不能把 latest
  回退到 earliest 当作同圈证明，也不能把单次 latch 可读当作逐圈保全。
- 独立复核的其他约束：latest 可能跳过 trailer 关联所需的前序本地证据、可靠命令
  或 ACK；未锁定 overlay 且 hint 仅有单帧稳定证据时，跨帧选择还会破坏相邻候选
  累积。最新几何完整候选仍需 Core0 完整 CRC，末候选损坏/变长/换相时不可默认
  它是最新有效帧。序列前跳可通过 receive-health，而 missing 计数是超时语义，
  不能据此认定中间帧均被消费。当前 scanner 与 adapter 测试分别注入 DMA 和已绑定
  edge，未覆盖真实硬件 helper 对旧 FIFO/新 packet 的组合。
- 处置与下一 gate：不直接替换全局 locate，也不以清标志或降低门禁作为完成。
  先在 TDMA owner 内验证由硬件事件产生的 epoch/事件序号或 DMA 位置与原始计时
  的联合记录，再以独立捕获身份匹配候选帧；expected sequence/identity 只能作为
  查询条件。边沿保全、普通镜像退休、可靠命令/ACK 消费分别证明，保留原有 FIFO
  overflow、DMA copy 后复验、缺样、STOP/重臂及 persona 退休。通过事件绑定负测、
  当前资源链接和四板原始记录后，才考虑已锁定固定布局的 follower 镜像 latest；
  普通 origin、bootstrap 与重同步继续独立验收。`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260914-026 — RX 站台等待与观察裁剪诊断

- TODO task ID：`VDC-TIME-002`、`VDC-RESOURCE-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。诊断切片通过当前源码 quick P3；未改变捕获/接受节奏，
  未接入正式时间输入，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-rx-wait-diagnostics-r2/` 的
  `rx-review-r1.json`、`resource-review-r1.json`、`review-final-r1.json` 和
  `p3-receipt-r3.json`。以下数字是本轮快照，非事实源；身份以源码及包 SHA 为准，
  增量目录复用的 build ID 不能单独区分本轮与上一固件。
- 实现：`tdma_service_timing.c` 在现有 Core1 phase/RESET 生命周期内汇总 station
  轮询与捕获后年龄、初次 DMA 观察间隔/积压，以及 epoch、clamp、stale hint、
  frame-copy、discovery-copy 五类丢弃；复用已有时钟读数。DMA epoch 丢失仍保留
  导致它的长观察间隔，STOP、运行类别、配置及 trial 变化才切断会话间隔；内部
  FSM 的高位变化不误拆同一会话。原有 DMA 复制复验、worker 退休及 STOP 取消保留。
- 数据来源：新增 `SYSTem:TDMA:PROFile:RX?` 在全部 STOP/ACK 后读取 SRAM 汇总，
  RESET 位于启动后的控制阶段，窗口延伸至 STOP；原生 TDMA 记录覆盖独立 ARM 的
  完整采样窗，全部 STOP 后顺序 SD SAVE/readback 并核对散列。RX 汇总没有写入旧
  原生记录 schema，不能将两种窗口的计数相等当作校验条件。station 年龄是 owner
  服务时刻的捕获后年龄，不等于 Core0 CPU 时间或准确的任务完成时延。
- 两轮从板结果：NO2/NO3/NO4 的 clamp 次数分别为 3139/3138/3138 和
  3132/3133/3134，其他四类 drop 均为零；每板累计裁剪 790617–802686 个观察
  words，不能换算成精确丢帧数。REQUESTED/BUILDING 轮询合计分别为 1/1/0 和
  5/1/0，占全部 station 轮询最高不足 0.08%。初次观察最大间隔分别为
  4485.076/4474.936/3153.708 µs 和 4535.772/4375.612/3143.744 µs；最大积压为
  1213/1215/1043 和 1213/1049/1043 words，超过本配置环形观察容量。间隔最大值
  与积压最大值未逐事件配对，不能据此反推精确 DMA 速率。
- 调度与全窗：从板完整相位峰值为 706.612–753.064 µs，两轮原生窗内 TDMA
  overrun/deadline 增量均零；对照上一切片 716–752 µs 的波动，未证明稳定提速。
  四板 RX ring overrun 增量分别为 0/6/1/10 和 0/3/4/2，missing 与 latch miss
  增量均零。NO1 TDMA overrun/deadline 仍为 497/471 和 488/466；启动拒收、
  原 TRN03 自主模式 gate false 与全部逐槽失败保留，不能关闭全窗或全表 WCET。
- 资源：首个上限容量构建失败是 `.data` 增加 128 B 越过对齐边界，导致 `.bss`
  后移 4096 B、RAM 超出 3484 B。将仅在 phase entry/exit 使用的 context 记录
  移回 Flash 后，最终 `.data` 相对基线增加 16 B、`.bss` 增加 264 B；容量 6/8
  的 A/B 链接均通过，RAM 余量为 4372/612 B，SCRATCH_X 数据占用仍为零。嵌套
  clock/record 探针保留 SRAM；编译器单函数栈报告已归档，不宣称动态整栈水位。
- 验证与失败：相关 host 测试 109 项通过，间隔边界和放置调整后重跑的 55 项是
  其中子集。首次 quick P3 用时 208.531 s，TDMA 闭环通过但 NO1 coded-marker
  completion timeout；resume 在 NO2 APPLY timeout 后于 11.078 s 终止。
  STOP/配置恢复后 r3 严格门禁通过，79.047 s 不含 build/OTA。一次离线 audit
  早于 SD summary 完成而失败，随后读取完整原件复核通过；本地 launcher 已增加
  上轮 SAVE terminal JSON 前置条件，保留该编排失败，避免只凭 STOP 放行下一轮。
- 下一 gate：观测支持优先检查“捕获/接受跨 service、窗口保留较旧帧”的积压，
  尚不支持将 Core0 构造等待定为主因。先评估最新完整帧选择与旧观察退休的有界
  策略，分别验证新数据准入、帧身份、copy 后 DMA 复验、hint 失效与 STOP/重臂；
  不将运输丢失与观察裁剪混为一谈。时间戳快速通道仍按 owner raw 身份与边沿
  条件单独验收，不能依赖整帧解析或以本轮诊断替代正式时间输入。

### VDC-PROGRESS-20260914-025 — TDMA review 06 的模式、身份及窗口复核

- TODO task ID：`VDC-TIME-002`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成用户指定 review 的只读证据复核；没有固件或板端操作，
  不改变 review 的 C11 状态、契约登记或正式时间输入门禁。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-review06-audit/review06-audit-r1.json`。
  原 review 单独归档；复核绑定其引用的 latch-direct-seed/handoff-r2 summary、
  origin 原生记录、完整逐槽报告、源码和上一封存。以下数字为该轮快照，非事实源。
- P1/P2 的主因判断与原件不符：summary 的 25 个相邻区间中，origin 的
  receive_rejected、bitmap_incomplete 和 receive_missing 增量全部为零，25 次
  可比较的 feedback identity 全部匹配。实际区间错误为
  `physical_flight_persona_mismatch` 25 次、`adapter_tx_not_growing` 24 次，来自
  旧检查器对普通 persona 和软件 TX 增量的假设；已由进度 021 的离线工具区分，
  原 TRN03 gate 继续保留 false。不能因此推导为本窗持续 bitmap 故障并直接修复。
- 身份与时刻混用：review 引用的不等 identity 属于 `runtime_before`，当时是普通
  origin persona 11，TX/RX sequence 为 276/275；相差 4,540,504 ns 的时间戳也来自
  该快照，且 ring_last_error 为零。它们没有同圈前提，不能据此证明自主模式持续
  关联倒置。`runtime_after` 已是自主 persona 16，TX/RX sequence 均 6369、identity
  均 191664741；运输身份一致仍不等于已获得有效的同圈物理时间戳。
- “时间戳前提闭合”未被证明：flags=2/resolution=8 ns 来自普通模式 before；自主
  after 是 flags=1（DIAGNOSTIC_ONLY）、resolution=0，reference TX 和 feedback RX
  时间戳均为零。origin 原生 slot 6–33 的 legacy latch_count 恒为 318，不能用
  切换前增长证明自主时间输入连续。`tdma_pio_spi_phys_origin.inc` 明确清除 CPU
  latch armed，由自主 graph 拥有 raw capture/rearm；量化分辨率不能替代实际边沿
  精度、逐圈关联或 formal qualification。
- WCET 与整窗边界：四板 DPLL cumulative max 在 summary 的 before/after 均相同；
  该 summary 覆盖期 NO1 新增 overrun/deadline 为 3/3，从板均零，不能写成四板本轮
  新增超限。完整原生窗口另保留启动 reject/bitmap 5/4/4/2 和 RX ring overrun
  0/6/3/0；解码错误为零不证明无观察覆盖、物理零误码或全窗已通过。摘要窗口与
  原生完整窗口、历史最大值与本窗增量必须分别引用。
- 采纳与下一 gate：保留 review 关于关联、共同时间、DPLL 实际更新预算和严格
  准入分别验收的检查清单；不采纳其未经模式/身份区分的 P1/P2 根因判断及物理层
  闭合结论。当前仍为 `VDC-TIME-002`：先补站台等待、DMA 初次观察间隔及 drop
  原因，完成 raw 身份/保持量和同圈计时前置条件；`VDC-TIME-003/004` 保持 PENDING。
  不提前接通命令、去除临时许可证、隔离健康节点或声明四板锁相。
- 工作区：外来 review 05/06 与索引更新保留；本轮 RX diagnostics 初始化在检查到
  外来文档散列变化时终止，尚未创建基线副本或修改固件，随后优先完成本次用户
  指定复核。终止记录与外来新散列已归档，下轮从新的工作区快照继续实现。

### VDC-PROGRESS-20260914-024 — RX 单站台吞吐与观察缓冲余量审计

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。仅完成只读源码及两轮完整原生窗口审计；没有新固件部署或
  板端操作，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-rx-consumer-capacity/` 的
  `capacity-audit-r2.json`；绑定上一封存、当前源码散列和六份从板原生记录，逐槽
  保留启动与后续失败。以下数字均为本配置的源码推导或测量快照，非事实源。
- 结构性限制：`tdma_pio_spi_ring_adapter_rx_once_impl()` 在 station 非 IDLE 时直接
  进入接受路径；REQUESTED/BUILDING 等待或 READY 接受均不捕获下一帧。即使 Core0
  立即完成，捕获与接受也至少分占两个 Core1 service。当前完整表周期为 1.5 ms，
  因此理论捕获上限约 333.333 次/s；不能用缩短一项 CPU 操作证明突破该限制。
  `task_refmem_sync()` 先运行 RefMem，再调用 Core0 prepare service，随后 delay；
  现有原生记录没有逐次 station 等待或捕获间隔，尚未证明具体等待来源及最大值。
- 两轮完整窗口：NO2/NO3/NO4 service 增量分别为 5499/5498/5498 和 5499/5500/5498，
  捕获为 2514/2537/2557 和 2508/2536/2560，约每个 service 捕获 0.456–0.466 帧。
  observation drop 增量为 2180/2202/2222 与 2193/2217/2242，明显多于 ring overrun
  的 5/0/0 与 6/3/0。drop 合并窗口裁剪、epoch、过期 hint 与复制复验等原因，
  不是丢帧数，也尚不能全部归为主动跳帧。从板本窗相位 overrun/deadline 仍均零，
  证明局部预算通过不足以保证观察吞吐或无覆盖。
- 容量边界：`TDMA_PIO_SPI_RX_RING_WORDS` 为 1024，每个 SRAM word 只承载一个
  观察字节；虽然占用 4096 B RAM，对当前物理帧 173 个观察字节仅约 5.919 帧容量。
  scanner 的最大窗口来自 `TDMA_PIO_SPI_RX_DMA_WORD_MAX` 加
  `TDMA_RX_OBSERVATION_SCAN_WORDS`，本次为 616 words；有效 hint 在裁剪后仍选取
  窗口内最早完整帧。复制本次 168-word 帧后，退休游标相对初次 produced 的积压
  至多 448 words；这是相对初次计数的几何量，必须再加入复制期间及下一次观察前
  的 DMA 增量，不能当作复制结束时的实时积压上界。初版报告命名未区分此时刻，
  已在 r2 更正并保留 r1；未将名义物理帧周期代入为实测保持时间。
- O2/O4 边界：DMA 初次观察建立 completed words 和 epoch，复制后复验排除期间
  的覆盖及 epoch 变化，两者不是可直接删去的重复采样。完整窗口的稀疏快照也不能
  给出逐圈物理周期、准确跳帧数或最大 worker 延迟。
- 下一 gate：`VDC-TIME-002`。先为 station 等待、初次捕获间隔和裁剪/epoch/复制
  拒绝补有界板端计数与最大值，独立核算 RAM 和整相位开销，再选择捕获节奏或
  明确的载荷合并策略；特等席时间戳与整帧解析解耦。同相位无条件追加捕获仍维持
  拒绝结论，身份、代际、DMA 复验与 STOP 取消必须保留。全目标与正式锁相不关闭。

### VDC-PROGRESS-20260914-023 — RX/TX latch 直接初始化与四板对照

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。当前重装切片已验收；完整观察连续性、正式时间输入及全表
  WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-latch-direct-seed/`。
  `current-plan.json` 绑定部署包与回退基线，`review-final-r1.json` 为总复核；
  提交身份和文件散列由 `commit-proof.json` / `slice-manifest.json` 绑定。
  以下数值均为测量或构建快照，非事实源。
- 实现：`tdma_pio_spi_phys_clock_latch_rearm()` 和
  `tdma_pio_spi_phys_tx_clock_latch_rearm()` 使用 CPU 注入的 `MOV X, ~NULL`，
  替代 FIFO 写入、PULL 和 OSR→X 搬运。既有 latch 程序只消费 X；保留禁用 SM、
  清 FIFO、restart、恢复 PC、记录 epoch、启用 SM 的次序。resident PIO 指令、
  owner/资源、静态 RAM 及接收/STOP 门禁不变，未重引入同相位补充捕获。
- 软件与目标：最终 latch 用例 15 项、相关观察/计时用例 89 项通过，共 104 个
  不重复用例；涵盖真实 C rearm/read、dirty state、无效输入、溢出和实际 PIO 源码
  的首边沿/FIFO 保留行为。容量 6/8 的 A/B/boot 增量构建分别为 27.328/27.547 s；
  目标反汇编均为 `0xa02b`，重装中不再有 FIFO 传送轮询。两包各减少 72 B，主 RAM
  余量分别 4636/876 B、SCRATCH_Y 使用 1448/1648 B，SCRATCH_X 数据分配为零。
  RX/TX rearm 的编译器局部栈为 32/40 B，不代表完整调用链或动态水位。
- 源码提交：`447b93c`。源码指纹为
  `836c8e42d6bd842ed18ab24da2ea71e2abd83ca5695073677561f91f54578b82`，
  共 1069 个受验文件；当前容量 build `20260914112635` 的包 SHA-256 为
  `3eb8c621be8499da61afef1f422a2d0a62b5e0d0bc9046d67dcf29dbe59ef6b7`。
  增量 build ID 与基线相同，不能单凭 build ID 区分包；以源码和包散列为准。
- P3 失败与恢复：首轮四板 OTA/quick 流程为 195.688 s，普通 TDMA 三项 gate
  通过，但 NO2 coarse CLK APPLY 失败、TRN01 SCK gate 失败及无可用 rearm margin
  行导致 strict=false。第二轮 resume 在 P0T 的 NO1 profile APPLY 失败，10.906 s
  退出且没有 receipt。STOP 后 NO1 的 stage/apply/reject 为 1/0/1、last_result 为
  BAD_ARGUMENT，不能归为单纯丢 ACK；一次有界 STAGE/APPLY 重试成功，具体拒绝
  分支尚未证明。第三轮用同一包及已验证 OTA 的正式 resume，81.797 s，strict=true、
  diagnostic failures 为空，普通 TDMA passed/closed_loop/realtime 均 true。
  恢复耗时不含 build/OTA，不能冒充全流程加速结果；三轮原件和恢复读回均保留。
- 自主有限采集：两轮各板原生样本均为 34 条、无漏采；四板 missing 和 latch miss
  增量均零，从板 TDMA overrun/deadline 增量均零。NO2/NO3/NO4 OTHer 完整相位峰值
  两轮分别为 716.452/732.512/737.296 µs 和 725.280/737.060/752.080 µs，均在当前
  配置预算内；回退对照为 740.984/749.244/722.224 µs。RX/TX 重装分项涨跌不一，
  这些是整相位峰值内的分项，非独立 stage 最大值或时延分布，稳定微秒收益未证实。
- 未闭合反证：NO1 两轮 TDMA overrun/deadline 为 484/454 与 459/433；四板 RX ring
  overrun 分别为 0/5/0/0 与 0/6/3/0，reject 两轮均 5/4/4/2。原始自主三项 gate
  均 false，逐槽审计也未通过；不能用缺失计数为零代替 RX 无覆盖或全窗连续性。
- 存储和观测边界：START 至最终 STOP 无 SCPI 查询，全部 STOP/ACK 后逐板 native
  SD SAVE/readback，各轮均成功且字节匹配；命令时间证明各板保存没有重叠。首轮
  STOP 后 profile 读回的 UNAVAILABLE 与工具异常保留，工具改为保留原响应并有界
  重试，后续读回完整。最终四板 STOP/config ACK/phase/grant inactive，未操作 NO5，
  未隔离健康 TDMA 节点。外来文件散列和上一封存均复核未变。
- 下一 gate：`VDC-TIME-002`。继续拆分 DMA 初次观察、复制后复验及跨 service 消费
  间隔，解决 RX 覆盖、普通 origin 超限及可恢复 APPLY 拒绝；保留代际/复制一致性
  与 STOP 取消，不以扩大同相位工作或关闭诊断使门禁通过。未接通新的 DPLL 正式
  时间输入，不声明四板锁相。

### VDC-PROGRESS-20260914-022 — RX 同相位补充捕获的失败与回退

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。拒绝当前合并试验，恢复上一已提交实现；完整观察连续性和
  全表 WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 失败证据根：`out/HardwareAcceptance/20260914/dpll-rx-station-pipeline/`；
  `rejected-source-checkpoint.json` 绑定未提交源码、差异、测试及先前封存，
  `rejection-review.json` 保留逐槽增量与完整相位分解。回退复核单独保存在
  `out/HardwareAcceptance/20260914/dpll-rx-pipeline-rollback/`，不改写前轮证据。
  以下数值均为本轮测量快照，非事实源。
- 试验边界：process follower 接受一个 READY 结果、退休 station 后，在同一 service
  中至多捕获一帧交给 Core0。未增加 station、静态缓冲或资源借用，保留身份/代际/
  年龄检查和 STOP 取消。66 项相关 host 用例通过；前两次测试夹具错误保留，容量
  6/8 的 A/B 和 boot 构建通过，静态 RAM 不变。普通模式 quick P3 为 191.063 s，
  passed/closed_loop/realtime 和 strict 均通过，但不能覆盖自主试验中的回归。
- 自主试验：各板原生记录 34 条且无漏采，missing 增量均零；NO1 至 NO4 的 TDMA
  overrun/deadline 增量分别为 509/467、14/6、7/2、14/11，RX ring overrun 为
  0/34/15/3。原始 passed/closed_loop/realtime 均 false。从板超限分布于后续多个
  采样区间，不是只在启动时出现；平均接收速率提高不能证明最大观察间隔缩短。
- 完整相位：NO2/NO3/NO4 的 OTHer 峰值从 728.108/760.668/742.696 µs 升至
  881.324/908.356/894.040 µs，超过当前配置的 TDMA 850 µs 预算。峰值内 RX
  acceptance 为 121–173 µs、捕获为 188–213 µs，加上 request、overlay 和 owner
  其余工作，不能在同相位内无条件追加。stage 为嵌套区间，不能全部相加。
- 计时归因：四板累计 scheduler max 在采集首末相同，不能把历史最大值当成本窗
  新产生的峰值。NO1 两端均为自主 persona 的采样区间中 overrun/deadline 未增长，
  普通 origin/交接区间的失败仍计入完整窗口；跨板 slot 不作为共同时间对齐证据。
- 回退软件验证：源码指纹恢复为
  `ef5b67799fd11a2151750cca1d526ddc6f6322a986b4f6f9b91c0bf6945df20a`；
  两种容量重建的 package 和全部 ELF 与封存基线逐字节相同，89 项相关 host 测试
  通过。试验源码/测试已完整归档，其他工作区改动散列保持原样。
- 回退硬件复核：当前源码重新四板 OTA/quick P3 为 192.000 s，三项 TDMA gate
  及 strict 均 true、无 diagnostic failure。两次有限自主采集各板均 34 条、无漏采，
  从板 TDMA overrun/deadline 增量两轮均零；第二轮 NO2/NO3/NO4 完整相位峰值为
  740.984/749.244/722.224 µs。两轮 RX ring overrun 为 0/1/2/7 与 0/20/2/11，
  missing 均零，不能宣称观察缺口已修复。第二轮 NO1 TDMA overrun/deadline 为
  498/475，交接 7107.624 µs，原始自主三项 gate 仍 false。
- 存储失败与恢复：回退首轮 NO3 native SAVE 为 FAILED、storage error=6；四板
  RAM 原件完整，另三板 SD 散列一致。NO3 的一次 PEAK 查询返回 UNAVAILABLE，
  保留原响应和工具异常。软重启时 USB ClearCommError 也作为失败保留；随后按
  UID/build 验证 STOP/ACK，重启清空的校准相位须由下一轮重新装载，不能把重启后
  STOP 成功误写成校准通过。第二轮重新装载配置后的四板 STOP/相位检查通过，
  native SAVE 和 SD 字节一致性全部通过；未用主机回写副本冒充原生保存。
- 保存顺序勘误：本轮复核沿用的 `save_capture.py`，发现其实际使用四线程。
  前述切片关于“顺序保存”的描述不准确；保留原始命令时间序列和失败，未改写
  封存原件。回退第二轮改为逐板保存并由首末命令时间证明无重叠，耗时 41.094 s。
  本轮全部 START 至最终 STOP 之间仍无 SCPI 查询；最终四板 STOP/config ACK/
  grant inactive，未操作 NO5。总复核见回退目录 `review-final-r1.json`，失败试验的
  `slice-manifest.json` 标记 optimization_accepted=false；本轮仅提交文档进度。
- 下一 gate：保持独立的 RX 接受与捕获相位，先降低 latch 读取/重装、DMA 观察
  及现有单相位开销。重新合并前必须评估完整相位的剩余预算和后续保留量，不能以
  profiling 时钟代替 owner 的生产预算，也不能取消覆盖/代际/STOP 检查。当前
  `VDC-TIME-002` 不关闭，未接通新的 DPLL 输入，不声明四板锁相。

### VDC-PROGRESS-20260914-021 — 原生记录逐槽检查与接收观察缺口

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。离线诊断工具切片已验收；完整窗口连续性、同圈时间输入和
  全表 WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-observation-audit/`。
  `current-plan.json` 绑定源码、部署包与前轮封存；`review-final-r1.json` 为总复核，
  提交身份及逐文件散列由 `slice-manifest.json` / `commit-proof.json` 绑定。
  以下数值均为测量快照，非事实源。
- 实现：新增 `tools/calibration_ring_validate/tdma_observation_audit.py`，直接解码
  原生二进制并校验 CRC、build/board/采集 epoch、预期槽数/间隔、配置代际、persona/
  FSM、回传序号/身份、接收代际、FIFO 推进以及拒收/缺失/overrun 增量。全部采样槽
  和 baseline 计数增量都进入报告，启动失败和中途失败不能被后续恢复隐藏。跨组
  字段非同时采样，仅在序号相同时比较身份，否则明确记为不可比较。
- 模式边界：STOP 后的成功 HANDoff 必须与独立控制记录中的 trial/config/clock
  匹配，才能解释自主 persona 下的软件 TX 平台；实际回传、接收代际和 FIFO 仍须
  前进。普通 origin/follower 继续要求 TX 推进。该工具是独立诊断伴随工具，未修改
  TRN03 或 P3 门禁；它不证明逐样本 grant、逐圈 raw 身份、物理发车连续性、有效
  时间戳或 WCET。现有采样 schema 没有这些完整字段，STOP 冻结的尾部记录也不能
  替代整窗。`TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING` 单列为时间输入缺口。
- 软件与构建：173 项相关测试通过，使用生产 C recorder 输出验证停滞、回放/倒退、
  正向回绕、半区间跳变、身份/代际错配、CRC 损坏、缺少交接上下文以及启动和中途
  错误。容量 6/8 的 A/B、boot 增量构建分别 5.610/5.532 s，部署包及静态 RAM 段
  与前轮完全相同；主 RAM 余量仍为 4636/876 B。本切片只有主机工具/测试变更，
  不声称改变实时执行耗时。源码指纹为
  `ef5b67799fd11a2151750cca1d526ddc6f6322a986b4f6f9b91c0bf6945df20a`；
  build ID 仍为 `20260914112635`，包散列见 plan，结合当前 OTA 原件区分验收。
- 当前源码 quick P3：含增量构建和四板 OTA 共 192.468 s，短帧 passed/closed_loop/
  realtime 均 true，strict_gates_passed=true，无 diagnostic failure。STOP 后原生
  TDMA SD 保存和字节一致性通过；该普通模式通过不提升后续自主诊断的失败结果。
- 四板有限自主交接：trial/config 为 36/71，总交接 5904.296 µs；每板原生记录
  各 34 条、无漏采，NO1 有 28 条自主样本。启动阶段接收拒绝增量为 5/4/4/2，
  missing 均零，但 RX ring overrun 为 0/2/0/2；NO2 的早期接收未就绪也被保留。
  当前报告为 `dpll-observation-audit-handoff-r1-observations.json`，
  完整窗口 observation_checks_passed=false；原始 passed/closed_loop/realtime
  仍全部 false。全窗 NO1 TDMA overrun/deadline 增量为 460/435，从板两类增量均零。
- 旧证据复核：前轮两次原生文件经 SD 散列和控制记录重新绑定，未改写原件；RX
  ring overrun 增量分别为 0/2/10/7 和 0/1/5/12，missing 同样均零。这是原有
  观察缺口的新检出，不是本轮主机工具造成的固件回归。源码
  `tdma_pio_spi_phys_capture_words_async()` 在 DMA 写入超过未观察位置一个
  `TDMA_PIO_SPI_RX_RING_WORDS` 容量时累计 overrun，之后按有界新窗口推进游标。
  因而 missing 为零不能证明无接收观察覆盖；overrun 也不直接证明物理环路停发。
- 收尾与下一 gate：首次 START 至最终 STOP 之间没有 SCPI 查询，全部 STOP 后
  顺序保存原生 SD；最终四板 STOP/config ACK/grant inactive，未操作 NO5。
  继续 `VDC-TIME-002`，先定位从板 RX 消费积压/覆盖与启动拒收，同时保留普通
  origin 超限、准备/准入完整相位、交接及其他配置的门禁；补齐逐圈身份与实际边沿
  证据后才进入 `VDC-TIME-003/004`。本轮未接通新的 DPLL 输入，不声明正式锁相。

### VDC-PROGRESS-20260914-020 — 就绪阶段有界合批与取消后重臂

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。当前四板交接时间缩短，两轮 transport missing 增量为零；
  完整窗口的自主模式验证、普通 origin 超限及物理边沿误差仍未验收。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-ready-batch/`。
  实现提交：`60257e0`；文档单独提交，最终提交身份由证据 manifest 绑定。
  `initial-checkpoint.json` 绑定前轮封存与四板 STOP；最终源码和包见
  `current-plan.json`，逐阶段原件见 `handoff-r1-review.json` /
  `handoff-r2-review.json`，取消探针见 `cancel-r1-review.json`，总复核见
  `review-final-r1.json`。以下数值均为本轮测量快照，非事实源。
- 实现：`tdma_runtime_owner_origin_poll()` 按显式白名单合并已就绪的
  STOP→PERSONA→BUILD_BEGIN 和 BUILD_STEP→SEED→SMS→INSTALL。
  `TDMA_ORIGIN_PREPARE_BATCH_MAX_STEPS` 限定单次步数，
  `TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES` 到达后不再追加步骤；该阈值取 TDMA
  静态预算的一半，不等于最后一步或完整相位的 WCET 证明。每步重验 owner grant、
  配置代际、时钟、期限和 STOP，physical poll 继续核对冻结配置与安装资源。
  MAILBOX 与 STOP、BUILD_BEGIN 与 Core0 任务接收之间仍让出；未就绪的
  BUILD_STEP 一次读取后立即返回，不忙等，不提前复用仍有 DMA/worker 占用的 union。
- 软件与构建：33 项相关测试通过。新用例覆盖合批顺序、Core0 未完成、精确预算边界、
  五种可合并边界上的撤销/模型/时钟/配置/期限/STOP 变化，以及每阶段失败不能继续
  安装。首轮 expiry 用例预期同次 FAILED，但推进时钟也越过预算，实际先 BUSY
  让出；保留 `host-r1` 超时记录，修正为验证下一次调用前拒绝且没有后续 physical
  操作，`host-r2` 全部通过。固件未因此放宽门禁。
- 资源与部署：容量 6/8 的 A/B、boot 增量构建分别 8.359/8.296 s；主 RAM 余量仍
  为 4636/876 B，全部静态 RAM 段与前轮一致，SCRATCH_X 数据为零；owner poll
  静态栈从 40 B 到 64 B，physical poll 仍为 400/464 B，Core1 预留栈 2048 B。
  `.su` 不替代完整调用链或动态栈水位验收。当前源码指纹为
  `a8dfd83dac6184387894961d1250bfa0e93bbc65abc9d7eefe5a8f52f850f6e5`，
  四板部署包 SHA-256 为
  `0dc165fc11bf3540a247c35f16c825fbaec1e417f19f25bcda6bc15f56dc714f`；
  增量 build ID 仍为 `20260914112635`，必须结合 SHA 和 OTA 原件区分。
- 当前源码 quick P3：包含增量构建与四板 OTA 共 183.141 s，短帧 passed/
  closed_loop/realtime 均 true，strict_gates_passed=true，无 diagnostic failure；
  STOP 后原生 TDMA SD 保存及字节一致性通过。本轮没有重复初始化/校准超时，
  不表示前轮间歇故障根因已解决。
- 交接对照：两轮 trial/config 为 36/71、68/84；总交接由前轮
  12904.364/12735.268 µs 降为 8281.476/5889.468 µs。STOP 进入至 INSTALL 返回
  从 10478.576/10515.896 µs 降为 6167.708/3291.404 µs，分别缩短 41.14%/68.70%。
  STOP→PERSONA、PERSONA→BUILD_BEGIN 的体外间隙从跨调度周期缩到约数十微秒以内。
  r1 的 BUILD_STEP 观察三次，SEED 调用体 410.096 µs 后触发合批让出；r2 观察两次，
  SEED/SMS/INSTALL 同次推进。准备体内合计 1144.180/839.728 µs，体外合计
  7137.296/5049.740 µs，包含 Core0 构造、调度和等待，不全部当作空闲时间。
- 取消与恢复：两轮正常交接之间插入有限 BUILDCancel 许可证。真实 builder 发出
  一个 descriptor run 后暂停，公共 STOP 延迟取消一次，最终入口清零、worker IDLE，
  trial/config 为 52/77。`cancel-r1` 的传输门禁失败是主动取消试验原件；后续新代际
  handoff-r2 成功安装，证明本配置工作区可再次借出。该探针不证明任意指令边界竞态
  或微秒级取消上界，其余物理配置仍保留门禁。
- 连续性与检查器边界：两轮各板原生记录各 34 条、无漏采，transport missing
  增量均为 0/0/0/0。原始 passed/closed_loop/realtime 仍为 false；soak 的唯一
  错误类别来自 NO1 `physical_flight_persona_mismatch` 和 `adapter_tx_not_growing`。
  `trn03_closed_loop.py` 当前要求普通 origin persona 和软件 TX 计数，自主 persona
  的硬件发车不能套用该假设。保留失败结果，后续须用已授权 persona、硬件计数和
  原始计时身份建立完整窗口检查，不能简单允许任意 persona 或忽略 TX 停滞。
  missing 为零也不证明切换期间物理发车没有间隙。
- 完整相位：NO1 自主 RUN 峰值为 572.184/587.664 µs；普通 origin
  CYCLE_BOUNDARY→RUNNING 的峰值为 1579.056/1460.528 µs。全窗 TDMA overrun
  增量仍为 484/478，deadline miss 为 453/455，三块 follower 两类增量均零。
  合批后准备阶段的独立完整相位峰值尚未取得，不能以局部调用和 RUN 峰值关闭全表
  WCET；整表和 TDMA 预算仍由原有项目配置符号定义。
- 收尾与下一 gate：三轮首次 START 至最后 STOP 之间没有 SCPI 查询；全部 STOP
  后顺序保存原生 TDMA SD，字节匹配，最终 config ACK/grant inactive，未操作 NO5。
  继续 `VDC-TIME-002`，补齐独立准备/准入峰值和普通 origin 的 RefMem 发布分解，
  审核自主模式完整窗口检查，再验证物理边沿间隙及其余配置；不新增 DPLL 输入或
  宣布正式锁相，`VDC-TIME-003/004` 继续等待依赖。

### VDC-PROGRESS-20260914-019 — 校准 CRC 驻留与启动相位复核

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。当前源码 quick P3 与 CRC 等价性验证已通过，完整窗口连续性、
  普通 origin 超限及独立准入耗时仍未闭合；`VDC-TIME-003/004` 不提前开放。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-admission-crc-final/`。
  实现提交：`c2e6684`；本记录及 TODO 单独提交，提交身份由证据 manifest 绑定。
  `initial-checkpoint.json` 绑定前序邮箱合批切片和首个失败尝试；部署身份见
  `current-plan.json`，总复核见 `review-final-r1.json`，普通/自主相位增长见
  `phase-windows-reviewed-r2.json`。后者修正 r1 对状态方向的文字解释，数值未改。
  以下数值均为本轮测量快照，非事实源，不构成全表 WCET 或物理边沿误差界。
- 实现：`tdma_origin_calibration_crc32()` 使用与 Calibration 原 CRC 相同的
  CRC-32/ISO-HDLC 四位查表算法。准入仍重算完整 `tdma_ring_calibration_stage_t`，
  不缓存 CRC，不缩小校验范围；owner grant/config/clock/expiry、STOP 取消和 DMA
  退休边界保持。函数驻留主 SRAM，常量表放在 SCRATCH_Y 的 Core0 栈下方数据区。
  `TDMA_SERVICE_TIMING_VERSION` 新增 ADMIT、嵌套 CALIBRATION_CRC 和 BEGIN 阶段，
  解码工具兼容旧版本；未新增 PIO/DMA 或改变 DPLL 时间戳资格。
- 软件与资源：51 项相关测试通过，CRC 对照真实 portable OTA 库，覆盖容量 2/6/8、
  各长度和非对齐输入、完整 stage 逐位变化及 owner 准入拒绝。容量 6/8 的 A/B 与
  boot 增量构建均通过，分别 4.532/4.844 s。主 RAM 余量为 4636/876 B；CRC 函数
  60 B、表 64 B，SCRATCH_Y 数据为 1448/1648 B，距各自预留 Core0 栈底仍有
  600/400 B。Core0/Core1 栈各保留 2048 B，SCRATCH_X 数据为零。静态栈文件、
  ELF/map 和实际符号复核分别见 `build6-archive`、`build8-archive`、
  `stack-archive-r1.json` 和 `resources-reviewed-r1.json`。
- 失败与修正：首次把表也放主 `.data`，跨越 DMA 工作区对齐边界，容量 8 链接
  RAM 超出 3220 B。该次容量 6 已误启动的 P3 保留，短帧通过但 NO4 coarse CLK
  APPLY 超时，strict=false。原件封存于相邻 `dpll-origin-admission-crc/`，
  `attempt-manifest.json` 的 SHA-256 为
  `c7c222d201e6d0d25da7dd71eccfdc8a5c510093b18abf56c0f2d8b1e44bb8e4`；
  调整表的 RAM 落点后才形成当前容量 6/8 均通过的版本。
- 部署与恢复：当前源码指纹为
  `0ce28df0b5079b30d234f40c6f409b7cd98cba0983de2a8f1b4c5e758875e0d0`，
  包 SHA-256 为 `e7edb9c2a945376c3b156df48c56be33f14dce517f51bfd90a551fdccb685755`。
  增量 build ID `20260914112635` 与首个尝试相同，必须以源码/包 SHA 和 OTA 原件
  区分。当前 `p3-r1` 四板 OTA 完成，但 NO2 在 P0T 的 APPLY 超时，115.266 s 后
  退出。原清理器把未初始化 grant/校准偏移也判为失败，实际 STOP 已响应；另存
  lifecycle-only 复核，显式撤销许可证后四板 stopped/config ACK/grant inactive
  全部成立，不把未初始化偏移认定为校准通过。
- 当前硬件验收：`p3-r2` 使用正式 `resume` 入口，复用当前包和成功 OTA，并重跑
  软件复位、拓扑、校准及短帧闭环。80.235 s 完成，短帧 passed/closed_loop/
  realtime 均 true，strict_gates_passed=true，无 diagnostic failure；STOP 后
  原生 SD 字节校验通过。该时长是跳过 build/OTA 的恢复流程，不能写成全流程耗时；
  未增大超时或修改门禁，初始化间歇超时的根因仍未解决。
- 自主交接对照：两轮总交接为 12904.364/12735.268 µs，相对前轮一增一减，不能
  宣称交接总时长改善。STOP 进入至 INSTALL 返回为 10478.576/10515.896 µs，
  准备调用体合计 751.716/1118.864 µs，其余包含跨周期调度、Core0 工作和等待。
  四板每轮各记录 34 条、无漏采，transport missing 增量均为 1/1/1/1；两轮完整
  窗口三项门禁均 false。首次 START 至最后 STOP 无 SCPI 查询，全部 STOP 后
  原生 SD 保存及字节一致性复核通过，临时 grant 已撤销，NO5 未操作。
- 耗时构成：两轮 NO1 自主 RUN 峰值为 552.520/657.524 µs，OTHER 峰值为
  1458.756/1490.044 µs。后者 context 769→513 对应普通 resident
  CYCLE_BOUNDARY→RUNNING，其中 RefMem 发布为 557.676/601.760 µs。所选峰值的
  CRC/BEGIN calls 均为零，因此只能说明峰值已落到其他调用，不能据此给出 CRC
  的单独耗时或提速倍数。全窗 TDMA overrun 增量为 490/471，deadline miss 为
  473/446；从第一条自主样本到末样本两类增量均零，不能用该子段替代整窗验收。
  整表/TDMA 预算仍由 `PROJECT_CORE1_PROFILE_1500US_CYCLES` 和
  `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES` 定义，板端快照为 1500/850 µs。
- 下一 gate：继续 `VDC-TIME-002`，独立捕获成功准入而不依赖总峰值选中，定位普通
  origin 的 RefMem 发布及完整相位成本，压缩 STOP→INSTALL 的有界调度间隙；
  补齐边沿/SD 波形和其他物理配置，保持 DMA 退休、CRC/grant 和取消门禁。
  本轮未接通新的自主 DPLL 输入，不能声明四板实际输出锁相。

### VDC-PROGRESS-20260914-018 — 冻结邮箱有界合批与交接对照

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。邮箱合批已取得当前拓扑的时延对照，完整交接连续性、原点相位
  超限及其他物理配置继续保留门禁；`VDC-TIME-003/004` 和命令接线仍未开放。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-mailbox-batch/`；
  `current-plan.json` 绑定前序 manifest、STOP、源码和固件包，逐轮记录见
  `handoff-r1-review.json` / `handoff-r2-review.json`，对照见 `review-final-r1.json`。
  以下数值均为本轮测量快照，非事实源，不构成 WCET 或物理边沿误差界。
- 实现：`tdma_pio_spi_phys_origin_mailbox_batch()` 每次最多检查
  `TDMA_ORIGIN_PREPARE_MAILBOX_BATCH` 个冻结邮箱，工作量不随编译容量无限增加。
  仍逐个验证 magic/version/class/source/target/CRC；检查长度、容量和游标后才寻址。
  STOP 保留在下一 poll，继续经过 owner grant/config/clock 复验，旧 DMA 退休后
  才复用 persona union。未增加 PIO/DMA、静态 RAM、时间戳资格或 DCO 应用。
- 软件与构建：29 项相关测试通过；新测试在编译容量 2 至 8 下覆盖全部准入节点数、
  每邮箱逐字节损坏、重算合法 CRC 后的非法结构/来源/目标、后批损坏不能提前读取、
  长度/NULL/游标/容量越界和输入不变。既有 grant 撤销、代际/clock/expiry 变化、
  STOP 和构造任务取消回归继续通过。容量 6/8 的 A/B 与 boot 增量构建分别
  7.718/7.219 s；预留堆后 RAM 余量仍为 4716/956 B，physical poll 静态栈仍 400 B，
  SCRATCH_X 无数据增长。容量 6 实板使用四节点；其余物理节点配置不继承 HIL 结论。
- 部署身份：增量构建保留 build ID `20260914104205`，不能只凭 build ID 区分切片。
  本轮源码指纹为 `c5d553099a2e1babbe10423ee8ef61a34fbf96397eec4fb973db5fb531b42355`，
  新包 SHA-256 为 `b65a4b27ef35dd638d362f1693b935a6b9653c529be3102c9bec87bf3944e4d1`；
  archive、当前包、P3 凭证与四板 OTA 完成记录共同绑定本轮固件。
- 四板 quick P3：外部 196.578 s，短帧 passed/closed_loop/realtime 三项均 true，
  本轮 strict_gates_passed=true，无 diagnostic failure。四板原生记录各 14 条、
  无漏采，STOP 后 SD 字节一致。前轮严格校准超时仍保留在其封存目录；本轮通过
  不表示该间歇性失败根因已修复，也不关闭全表 WCET。
- 交接对照：两轮 NO1 trial/config 分别 36/71、52/77；软件总交接由前轮
  16868.584/16933.140 µs 降至 12444.264/12996.216 µs，分别缩短 26.23%/23.25%。
  mailbox 调用次数从各 4 次降为各 1 次，首次 mailbox 至 STOP 从约 6 ms 降至
  1511.436/1501.712 µs；合批调用体为 23.872/94.024 µs。第二轮初始等待
  1003.964 µs，比第一轮 429.964 µs 更长，不把总时差全部归因于 CRC 或合批。
  全部准备调用体为 690.480/1198.268 µs；STOP 进入至 INSTALL 返回仍为
  10502.864/10490.540 µs，缩短邮箱检查尚未缩短这段软件停环区间。
- 预算边界：本轮板端整表为 1500 µs，TDMA WCET 预算为 850 µs，事实源分别为
  `PROJECT_CORE1_PROFILE_1500US_CYCLES`、`PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`
  及板端时钟；不是早期整表方案的 500 µs。两轮邮箱调用体小于该预算，但 NO1
  完整 TDMA 相位峰值为 1558.196/1937.848 µs，全窗 overrun 增量 497/472、
  deadline miss 增量 474/446。三块 follower 该相位两类增量为零。稀疏原生样本
  与单个 peak 不能证明所有调用的 WCET，更不能以邮箱局部耗时代表完整相位通过。
- 连续性与收尾：每轮四板原生记录各 34 条、无漏采，首次 START 至最后 STOP
  之间无 SCPI 查询，STOP 后 SD 字节一致。r1 的 transport missing 增量依 NO1
  至 NO4 为 1/1/0/0，r2 为 1/1/1/1；两轮完整窗口三项均 false，不提升稳定子段。
  最终四板 STOP/config ACK、临时 grant inactive，NO5 未操作；未采集新的 DPLL
  trace 或物理边沿波形，不能据此声明实际输出锁相。
- 下一 gate：在 `VDC-TIME-002` 内继续定位 STOP→INSTALL 的交接间隙与 NO1 完整
  相位超限，补齐板端边沿/SD 波形证据和其他物理配置；保留 grant/CRC 拒绝、DMA
  退休与构造取消门禁，完整切换窗口闭合后才进入后续时间输入准入。

### VDC-PROGRESS-20260914-017 — 自主 origin 交接分阶段计时

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。完成软件交接计时切片；完整窗口连续性未通过，尚未测得物理
  边沿间隙或通用 WCET，不开放 `VDC-TIME-003/004` 和命令接线。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-handoff-timing/`；
  `current-plan.json` 绑定前序 manifest、STOP、源码与部署包；两次复核见
  `handoff-r1-review.json` / `handoff-r2-review.json`，主控汇总见
  `review-final-r1.json`。以下数字均为本轮测量快照，非事实源。
- 实现：`tdma_origin_handoff.c` 由 Core1 单 writer 记录每个准备阶段的首次进入、
  调用次数和累计调用体耗时，guard 保护跨核快照；DONE/FAILED 冻结到下一有效 begin。
  `READ:CALibration:ORIGin:HANDoff?` 仅 STOP/config ACK 后可读，BUSY 表示未完成。
  调用体包含 owner 授权复验和 physical poll，记录器 bookkeeping 在计时体之外；
  总交接包含跨周期等待、Core0 构造与排队、其他任务和仪器开销，不能全部称为空闲。
  本切片没有修改 grant 准入、wire、PIO/DMA 或 DCO，也没有测量 Core0 builder CPU。
- 软件与资源：相关测试 22 项通过，含时间低字回绕、累加/终态冻结、非法顺序/溢出、
  grant 撤销和 STOP 读回准入。容量 6/8 的 A/B 与 boot 增量构建通过，分别为
  22.359/21.875 s；记录器增加 BSS 160 B，预留堆后主 RAM 余量为 4716/956 B。
  SCRATCH_X 未增加数据；目标与资源原件见 `capacityN-checkpoint.json`。
  部署容量 6，build `20260914104205`；源码指纹为
  `7912871406ab71ad0b55cf88f6694e6f23dc1fbdee87264a533bd72287f04ff3`。
- 四板 quick P3：外部 184.656 s，短帧 passed/closed_loop/realtime 三项均 true，
  四板原生记录各 14 条，无漏采，STOP 后 SD 字节一致。但 strict_gates_passed=false：
  coarse CLK level 7 时 NO2 的 `SYSTem:TDMA:RING:TOPology 4,1,1` 超时，随后返回
  `-200,"Execution error"`，失败原件保留于 `p3-r1/diagnostic.json`。诊断流程完成
  及 quick 凭证有效不代表严格校准通过，未重复 P3 覆盖该失败。
- 两轮自主交接：NO1 trial/config 为 36/64 和 52/70，总时长分别 16868.584 和
  16933.140 µs；累计 Core1 调用体为 1213.440 和 1146.284 µs，体外时间为
  15655.144 和 15786.856 µs。四次 mailbox 检查调用体仅 163.152/134.004 µs，
  从首次 mailbox 进入到 STOP 阶段进入却跨 5982.020/5914.160 µs。STOP 阶段进入
  到 INSTALL 返回为 10554.988/10622.972 µs；这些软件边界不等于 wire 边沿间隙。
- 完整窗口：每轮四板原生记录各 34 条、无漏采，NO1 各有 28 条 persona 16 样本，
  接收序列持续增长，全部 STOP 后 SD 字节一致。两轮四板 transport missing 均各
  增加 1，完整窗口短帧/连续性三项均 false；不得提升稳定子段或以记录无漏采掩盖
  传输 missing。首次 START 至最后 STOP 之间无 SCPI 查询；最终四板 STOP/config
  ACK，临时 grant inactive，NO5 未操作。未增加 DPLL trace 或正式时间戳资格。
- 下一 gate：先在 `VDC-TIME-002` 合并有界的冻结邮箱检查，验证准入容量的相位预算、
  坏 CRC 拒绝、每次 poll 的 grant 复验及 STOP 生命周期，再复测完整切换窗口。
  四次检查合为一次名义上可减少三个静态周期，但这是待验证估算；其余准备动作仍需
  分阶段，不能将约 1.2 ms 的累计调用体整体塞入单个 TDMA 相位。

### VDC-PROGRESS-20260914-016 — 构造中取消的板端诊断探针

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。补齐当前四板拓扑上 NO1 构造任务的确定性取消证据，父任务
  继续保留其他硬件配置与交接时延门禁；不提升自主时间输入或正式锁相状态。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-build-cancel-probe/`；前序 manifest
  由 `initial-checkpoint.json` 绑定，目标身份见 `current-plan.json`，逐轮复核见
  `probe-r1-review.json` / `probe-r2-review.json`，主控复核见 `review-final-r1.json`。
  以下数字均为本轮快照，非事实源。
- 实现：新增显式有限诊断 `CALibration:ORIGin:TRIAL:BUILDCancel`，通过既有
  Calibration grant 和 Core1 owner 准入，冻结 trial/config 代际。Core0 在真实
  builder 的 emitting pass 写出首个描述符后让出执行；Core1 在 BUILD_STEP 发现
  PAUSED 后进入现有故障／STOP 路径，STOP 未得到 worker ACK 时保留 workspace。
  Core0 仅恢复带 PAUSED 标记的 CANCELLED 任务，清空 entry 并先撤销活跃标记再
  发布 IDLE。普通 TRIAL 不启用探针；没有新增 PIO/DMA、时间戳资格或 DCO 应用。
- 诊断读取：`READ:CALibration:ORIGin:BUILDCancel?` 仅在 ring STOP/config ACK
  且 worker IDLE 后可用，读取保留的清理结果，不再访问已复用的 persona union。
  此接口记录生命周期事实，不提供物理边沿时间或微秒级取消时延上界。
- 软件：相关测试 20 项通过；容量 2/6/8 的真实构造矩阵额外覆盖 10/50/70 组探针
  场景，共 130 组，包括暂停前后取消、取消早于 claim、旧 PAUSED 状态不能复活
  writer，以及 poison 后重新构造逐字节一致。此前 5824 组逐块取消继续通过。
  记录与命令分别见 `host-r1` / `host-r2`；本轮没有用 host 调度交错代替芯片测量。
- 资源与构建：容量 6/8 的 A/B 与 boot 增量构建分别耗时 17.985/17.500 s。
  新增静态 RAM 32 B；预留堆后主 RAM 余量为 4876/1116 B；SCRATCH_X 未增加数据，
  详见两份 `capacityN-checkpoint.json`。未测容量不继承本轮目标资源或 HIL 结论。
  当前部署容量 6，build `20260914101340`；源码指纹为
  `8b55a9557bc99ec7bde13b7ddd7be38962fe03efdddc98efb1334f2d67779173`。
- 四板 quick P3：内部 184.464 s、外部 184.656 s，构建复核 2.945 s、OTA 105.547 s；
  strict_gates_passed 和短帧三项标记均 true。四板原生记录各 14 条，无漏采，STOP
  后 SD 字节一致。quick 时间不包含首次增量编译及额外诊断实验，也不证明整表 WCET。
- 两轮板端探针：NO1 的 trial/config 分别为 36/71 和 52/78，均在 emitting pass
  写出 1 个描述符后暂停，记录 1 次取消未确认、入口清空成功、最终 RETIRED/IDLE。
  第二轮新代际证明已重新 ARM 并建立新构造任务。每轮四板原生记录各 34 条，无漏采，
  全部 STOP 后 SD 一致；采集期间无 SCPI 查询。这里只证明 NO1 作为 origin、当前
  拓扑和固定构造块的暂停取消，不宣称所有板卡角色或任意指令竞态已实测。
- 保留失败：两轮主动取消的连续性三项标记均 false；r2 另有
  `explicit startup barrier timed out`，不得由取消清理成功覆盖。P3 后的辅助封装
  曾因命令日志与 STOP 结果同名退出，实际 STOP 成功；失败原件保留于
  `p3-finish-r1.json`，后续命令日志改用独立名称，SD 保存已完成。
- 恢复对照：普通短帧 r1 已在约 1.26 s 内取得三个连续健康样本，但 NO1 最后一个
  原生样本被主机 STOP 取消，终止原因 2、33/34 条，因 collection error 汇总为
  启动屏障失败。保留 r1 原件；本轮 `collect_normal.py` 在停止截止时间中计入既有
  启动触发预算，未改变启动健康门限，也未增加实时查询。r2 外部 41.844 s，四板各
  34 条且无漏采，启动屏障和短帧三项标记均 true，STOP 后 SD 字节一致。该恢复
  使用普通 origin，不能代替自主时间输入；NO1 TDMA overrun、上游迟到与部分 DPLL
  相位超限仍保留于原始记录，不宣布全表 WCET。最终四板 STOP/config ACK，临时
  许可证 inactive，NO5 未操作。
- 下一 gate：`VDC-TIME-002` 的其他硬件配置与实测交接时延上界，然后进入
  `VDC-TIME-003` 的完整窗口连续性与边沿误差界；`VDC-TIME-004` 和命令接线仍未开放。

### VDC-PROGRESS-20260914-015 — 真实 DMA 构造块取消与复用补证

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。完成当前软件补证，不关闭实际硬件配置/取消门禁。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-build-cancellation/`；前序 manifest
  由 `baseline-plan.json` 绑定，当前身份和主控复核见 `current-plan.json` /
  `review-final-r1.json`。以下计数与耗时均为本轮快照，非事实源。
- 变更：扩展 `test_tdma_origin_build_job.py` / `tdma_origin_build_graph_cases.c`，
  生产 builder 仅在 host 编译时重命名单步入口，真实 Core0 job 经过包装器在每个
  块前后注入取消。覆盖最终块已写出 entry、worker 尚未发布 READY 的窗口；取消
  返回 false 时禁止 take/request/复用，worker 退休后 entry 全部失效；将 builder
  和输出填入 poison 后，迟到入口无写入，再构造与正常图逐字节一致。固件未修改。
- 软件结果：容量 2/6/8 分别通过 448/2240/3136 组，共 5824 组；对应 26 张基准图，
  覆盖各容量下的运行节点数、local slot 0、连续 active mask 和记录开关。未将此
  子矩阵扩称为所有物理配置。连同既有三层 DMA STOP、物理 owner 退休、raw 和准入
  测试共 19 项通过。原有 mock 写入中断与本次真实块边界互补，均不是芯片实测延迟。
- 反向验证：仅在 `out/` 副本去掉最终取消清理，真实构造测试触发断言；原始退出码
  和 stderr 保留于 `mutation-result-r2.json`。r1 辅助探针因 pytest 临时目录名错误
  未编译，失败原件保留；修正入口后成功检测反例，没有修改生产代码以制造失败。
- 构建与身份：复用容量 6 live build，构建复核通过；A/B map/ELF/package 归档于
  `build-archive/`。build 仍为 `20260914093110`，包 SHA 与前序完全一致；测试变更
  后源码指纹为 `969902c55fc43dc652dc7feb60dbe8845245e2528cddfb0d30581e3f25eb6365`，
  为该指纹重新执行 P3，不沿用前序 receipt。
- 四板 quick：内部 181.431 s、外部 181.656 s；构建复核 2.401 s、OTA 106.033 s。
  strict_gates_passed 和短帧 passed/closed_loop_passed/realtime_gate_passed 均 true。
  四板原生记录各 14 条、无漏采，全部 STOP 后 SD 字节一致；config ACK、临时许可证
  inactive。SCPI 仅控制流程，未操作 NO5。原始调度计数仍单独保留，quick 聚合通过
  不等于整表 WCET、前序间歇 SCK 失败根因或正式锁相已闭合。
- 下一 gate：`VDC-TIME-002` 的芯片上构造中取消、配置切换及有界交接；之后才开放
  `VDC-TIME-003/004`。本轮没有新增自主 timestamp、命令运输或 DCO 应用。

### VDC-PROGRESS-20260914-014 — RefMem 向量快照收敛与两种容量快速验收

- TODO task ID：`VDC-TIME-002`、`VDC-SCHED-001` 的前置资源/调度修复。
- 状态：IN PROGRESS。当前切片完成，父任务未关闭，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-refmem-vector-projection/`，主控复核见
  `review-final-r1.json`，身份见 `current-plan.json` / `capacityN-checkpoint.json`。
  以下容量、耗时、帧大小及计数均为本轮快照，非事实源。
- 变更：VDC owner 新增 `vdc_dpll_manager_get_vector_snapshot()`，在既有发布 guard
  内有界复制旧 RefMem 向量实际消费的字段；排除完整 path table、observation matrix
  和无关诊断。RefMem 仍由 Core1 发布，保留代际准入、交替向量、CRC 和 seqlock；
  未增加静态副本，未改变 wire 布局、PIO/DMA、DCO owner 或从机命令接线。
- 软件：容量 2/6/8 的生产 getter/fill 提取测试，与固定旧源码生成的每容量 32 组
  向量 CRC 对照通过；另验 NULL、无效发布、奇数 guard、有界重试和复制后写入/回绕。
  这属于序列化 CRC 对照，不宣称逐字节穷举等价。相关 Python 共 22 项、原有 RefMem
  VDC vector host C 测试通过，命令和日志分别见 `projection-tests-r2` / `vector-tests-r1`。
- 目标资源：容量 6/8 的 A/B 与 boot 均通过，投影在 ARM ABI 下均为 632 B，原完整
  snapshot 分别为 1384/1584 B。容量 8 旧 realtime 和嵌套向量 helper 栈帧已合计
  2088 B，超过保留的 2048 B Core1 栈；这尚不证明实际内存破坏或超时因果。新 helper
  被编译器内联，realtime 帧为 1056 B，getter 为 48 B；未将 libc/ROM、调度祖先和
  中断嵌套计入整栈证明。`.su`、反汇编和 map 均保存，SCRATCH_X 不新增数据。
  主 RAM 预留堆后余量仍为容量 6 的 4908 B、容量 8 的 1148 B。
- 构建失败：r1 辅助配置覆盖 SDK CPU flags，汇编失败；r2 RAM 代码增长使 BSS 跨越
  对齐边界，容量 8 链接溢出。两次未部署，日志及失败 map 保留。合并重复向量 flags
  计算后增量 r3 通过，容量 8 `.data` 相对旧版本减少 72 B，未借用栈或其他 owner RAM。
- 当前源码指纹：`8e29cee4206bcb2d466e3c8c4f2e9f82f679f200528256df296d6de10b36d108`。
  容量 8 build `20260914093008`，容量 6 build `20260914093110`；package SHA 分别
  由独立 archive 和 checkpoint 绑定，P3 复核未更改归档包。仅操作 NO1–NO4。

| 配置 | quick 内部 / 外部耗时 | 构建复核 / OTA | strict_gates_passed | 四板 RefMem 保留峰值上限 |
|---|---:|---:|---|---:|
| 容量 8 | 185.370 / 185.625 s | 3.250 / 106.200 s | true | 72.728 µs |
| 容量 6 | 177.585 / 177.844 s | 2.731 / 98.860 s | false | 70.064 µs |

- 两轮短帧 passed/closed_loop_passed/realtime_gate_passed 均 true，各板原生记录
  14 条且无漏采，STOP 后 SD 字节一致。容量 6 的严格失败为 TRN-01 SCK 与 TRN-03
  replay 选行，最小 follower margin 为负；没有用本轮容量 8 成功覆盖该间歇失败。
  quick 使用可写增量构建目录；上述时间不包含首次构建和额外诊断采集，不能将本轮
  两种容量补测总和当作每次日常验收成本。与前序 513 s 相比，单轮流程约减少六成半。
- 实际更新补测：分别使用固定历史诊断矩阵、显式 provisional DPLL 和 clock evidence，
  普通 origin 下各板 26 条原生 TDMA 记录、76 条 DPLL trace，更新序列持续增长；
  first START 到 all STOP 无查询，TDMA SAVE 释放 StorageAO 后再保存 DPLL，读回一致。
  两容量四板 RefMem 自身 overrun/deadline miss 增量均零，未被隔离；保留峰值上限
  分别为 72.728/74.684 µs，低于既有 96 µs 预算。最终容量 6 的两种旧向量均读回
  非零 publish/source update sequence。此结果支持修复有效，不代替自主更新 WCET。
- 保留边界：尽管 quick 聚合标记与短帧通过，原始记录仍显示 NO1 TDMA overrun、
  VDC/DPLL/RefMem start miss；不得写成整表 WCET 通过。普通主板内部状态为 LOCKED，
  三从板为 CHECKING，命令接收和应用增量仍零；未验证自主时间输入、物理输出锁相
  或正式 quality。最终四板为当前容量 6 STOP、config ACK、临时许可证 inactive。
- 下一 gate：继续 `VDC-TIME-002` 的运行配置和有界取消；保留严格校准和上游迟到
  缺口。日常默认 quick、复用增量目录；OTA 已是主要主机耗时，进一步加速须以刷写
  各阶段原始计时另开工具切片，不能通过略过源码、设备身份或硬件判据放行。

### VDC-PROGRESS-20260914-013 — 编译容量矩阵和上限容量四板预采

- TODO task ID：`VDC-TIME-002`；`VDC-TIME-003/004` 保持 PENDING。
- 状态：IN PROGRESS。仅补证，无固件或正式时间输入变更，未接入从机命令。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-capacity-matrix/`，主控复核见
  `review-final-r1.json`。以下数字为本轮快照，非事实源。
- 身份：源码指纹与前序 quick 加速切片一致，仍为
  `87e98c7fef8e30d2607079b3d2c9946ec98c1eeb9f1bfcb1f99299bbc225aea2`。
  各容量 package、A/B map/ELF 和实际地址绑定见 `capacityN-layout.json`；容量
  6 的封存 build 仅读取。容量 2/8 同时构建产生相同时间 build 字符串，配置身份
  必须同时使用容量、package SHA 和布局，不能只按 build 字符串互换。
- 软件与资源：原始计时/记录/增量构造测试 54 项、准入测试 10 项通过。容量
  2～8 的 A/B 链接全部通过，保留堆、Core1 栈和既有 SCRATCH_Y 快照；12 个
  live origin 缓冲的真实地址、对齐和非重叠检查通过。实际地址构造共 1,939,224
  组，覆盖每个准入运行节点数/本地槽、连续 owner mask、tail/prefix、选定 guard/
  abort 和记录开关；单步最大 22 个描述符。非连续 active mask 的覆盖沿用前序
  host 矩阵，不扩张成本轮实板拓扑结论。

| 编译容量 | BSS | 预留堆之后主 RAM 余量 | 描述符峰值 / 分配 | literal 峰值 / 分配 |
|---:|---:|---:|---:|---:|
| 2 | 458796 B | 10196 B | 274 / 320 | 125 / 140 |
| 3 | 460088 B | 8904 B | 284 / 320 | 127 / 140 |
| 4 | 461404 B | 7588 B | 294 / 320 | 129 / 140 |
| 5 | 462728 B | 6264 B | 304 / 320 | 131 / 140 |
| 6 | 464084 B | 4908 B | 314 / 320 | 133 / 140 |
| 7 | 466464 B | 2528 B | 324 / 352 | 135 / 140 |
| 8 | 467844 B | 1148 B | 334 / 352 | 137 / 148 |

- 四板 quick：容量 8 build `20260914085410` 配置为四节点运行，NO5 未操作。
  内部流程 218.486 s、外部命令 219.500 s，增量复核 9.573 s、OTA 111.963 s。
  此时其他容量仍在编译，不能把与前序 185.790 s 的差异归因为固件容量。receipt
  流程完成，但 `strict_gates_passed=false`；启动稳定门通过，NO1 RefMem 曾达
  26100 cycles，超出 24000 cycles 预算，随后被既有调度器隔离。四板原生 SRAM
  各 14 条、无漏采，STOP 后 SD 一致；短帧严格失败保留，不能用诊断完成替代。
- 自主预采：有限许可证下各板 34 条原生记录；切换期间每板一次真实 missing，
  完整窗口失败。旧普通 persona 判据的 mismatch 另行保留。预选 3～6 s 诊断
  窗口没有新增 missing/reject/TDMA overrun，不能替代整窗。NO1 原始记录 epoch
  1、sequence 8149～8155 连续，timer 读取夹区为 252 ns，TDMA 完整相位峰值
  603.020 µs；这些原始计时不是物理边沿或共同时间。自主 DPLL trace 为零，命令
  接收/应用增量仍为零。原生 TDMA SD 与主机导出后写入 SD 的 raw 副本分开核验，
  后者两次读回一致。
- 恢复与限制：容量 8 普通模式恢复后，旧 raw 各 age 均 UNAVAILABLE；运输闭环
  通过，四板各 26 条记录。此入口未要求 DPLL schedule gate；NO1 中段 TDMA
  overrun 119、VDC start miss 105，不能宣称全表 WCET 通过。随后刷回容量 6
  build `20260914084228` 并完成普通短帧恢复及 SD 一致性；同样保留中段 TDMA
  overrun 176、VDC start miss 191。最终四板 STOP、config ACK、临时许可证
  inactive，运行期间无 SCPI 查询。
- 辅助脚本失败：首次回退 STOP 导出缺少 checkpoint 指纹；补齐后冷启动尚无
  staged phase，部分 origin 查询 UNAVAILABLE。两次失败原件保留。单独核对
  冷启动 STOP/config ACK 后再装载矩阵，最终配置后的相位/许可证检查全部通过；
  没有把冷启动缺省值冒充已装载配置。
- 下一 gate：`VDC-TIME-002` 的对应物理拓扑/运行配置、有界取消和容量 8 RefMem
  失败仍未闭合。目标链接缺口已补齐；真实总线/边沿、切换连续性、同圈输入和正式
  锁相继续按原依赖推进。日常切片复用匹配配置的可写构建目录并使用默认 quick；
  本次容量矩阵作为独立补证，不加入每轮快速验收，也不重建已封存产物。

### VDC-PROGRESS-20260914-012 — quick 验收目录扫描加速

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001` 的快速迭代支撑切片。
- 状态：扫描加速和当前四板 quick 验收完成；`VDC-TIME-002` 保持 IN PROGRESS，
  长期锁相目标未完成。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/p3-quick-scan/`，对照前序
  `dpll-raw-capacity/p3-r1/timing.json`。以下时间和数量均为快照，非事实源。
- 原因与变更：此前已经使用 QUICK_DIAGNOSTIC；主要开销来自 USB 命名空间和
  Flash 清单检查先遍历历史产物再过滤，文档锚点扫描也进入历史目录。改为
  `os.walk` 下行前剪枝，保留源码违规规则；文档的 `SCAN_EXCLUDE_DIRS` 排除
  `out/`，其构建或测试副本不能补足真实源码缺失的锚点。未改 P3 采集数量、超时
  门限、OTA 块大小或短帧判据，未操作 NO5。
- 软件验证：旧实现的三处目录遍历由负测复现；改后相关 28 项测试通过，覆盖忽略
  目录不进入、真实违规检出、Windows C 扩展名匹配和仅旧产物存在的锚点拒绝。
  USB 检查为 0.453 s、Flash 清单为 0.531 s；文档回归从上一切片 139.250 s
  降到 0.546 s。Flash 清单结果与之前完全一致。检查器和 skill 副本通过
  `--skill-sync`，登记表模板同步现有 canonical；没有变更契约登记状态。
- 构建身份：源码指纹
  `87e98c7fef8e30d2607079b3d2c9946ec98c1eeb9f1bfcb1f99299bbc225aea2`，build
  `20260914084228`。新容量 6 首次完整构建为 93.469 s，A/B RAM、PIO 程序和
  Flash 清单与前序一致。package 与 map 绑定见 `source-checkpoint-r2.json`。
- 同类 quick 对照：增量构建复核由 326.346 s 降到 3.190 s；含该构建复核、四板
  OTA、复位、拓扑、校准与短帧的 P3 内部总时长由 513.447 s 降到 185.790 s，
  缩短 327.657 s，约 63.8%。外部命令总耗时为 186.062 s，四板 OTA 占 105.978 s。
  首次完整构建另计，两段实测合计约 280 s；不能把有 cache 的 quick 耗时当作
  从空目录首次编译的耗时。各阶段明细见 `review-final-r2.json`；初版 review 对
  重复阶段名称取最后一项，复核版已改为累计，原件保留。
- 硬件结果：当前源码 quick receipt 的 `strict_gates_passed=true`、失败列表为空，
  短帧 passed/closed_loop_passed/realtime_gate_passed 均为 true。四板各 14 条
  原生 SRAM 记录完整、无漏采，全部 STOP 后 SD 字节一致，最终 config ACK、
  临时许可证 inactive。该 quick 范围的门禁通过不代表 DPLL 命令、共同时间或
  正式锁相已完成。
- 下一 gate：返回 `VDC-TIME-002` 的剩余目标容量和硬件配置，再按既有顺序处理
  全窗切换连续性、边沿误差和同圈输入。快速迭代复用同配置构建目录，各轮验收
  证据另存；源码改变后仍运行当前指纹的 P3，不能用旧 receipt 或 replay 放行。

### VDC-PROGRESS-20260914-011 — 目标容量 RAM 缺口与 UI 状态副本收敛

- TODO task ID：`VDC-TIME-002`、`VDC-CONFIG-001`、`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：`VDC-TIME-002` 保持 IN PROGRESS；`VDC-TIME-003/004` 保持 PENDING，
  未关闭长期锁相目标。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-raw-capacity/`。`plan.json` 绑定
  前序 manifest，`current-plan.json` / `source-checkpoint-r2.json` 分别绑定两种容量。
  以下容量、字节数、组合数及构建号均为本次快照，非事实源。
- 原始失败：未修改 UI 时，容量 8 的实际 A 链接主 RAM 超出 2428 B；失败 map、
  日志和源码指纹保留于 `build-capacity8-r1`，没有部署该失败产物。B 链接尚未产生
  map，不能伪造修复前 B 对照，也不能从容量 6 的 host 测试推断容量 8 可部署。
- 资源修复：UI 只消费触发器的 19 个标量，却持有包含对齐序列表的完整 3072 B
  TriggerVector。Sync Trigger owner 新增 `sync_trigger_status_t` 和
  `sync_trigger_get_status()`，沿用现有临界区复制这些状态；UI 改为保存紧凑副本。
  原完整 getter、TriggerVector、序列表和硬件 owner 不变；未借用 Core1 栈或关闭
  原始计时记录。host C 测试覆盖空指针、未初始化、极值、锁内复制、解锁后源更新和
  UI 实际消费字段的等价性，`status-tests-r1` 通过。
- 目标链接：修复后容量 6/8 的 A/B 和 boot 均通过。ARM ABI 的 status 为 52 B；
  容量 6 UI 从 7168 B 缩到 3392 B，BSS 回收 3776 B，预留 2048 B heap 后主 RAM
  余量为 4908 B。容量 8 UI 为 3592 B，BSS 相对失败 A map 回收 3576 B，heap 后
  余量为 1148 B；新 A/B 主 RAM 布局一致。gdb 复核完整 TriggerVector 布局未变，
  SCRATCH_X 未分配数据，Core1 栈和 SCRATCH_Y/主核栈边界通过。详见
  `resource-review.json` / `source-checkpoint-r2.json`，回收空间尚未分配给命令缓冲。
- 配置几何：实际 owner 使用连续 active mask；在其固定 PIO/SM/DMA 分配下，覆盖
  每个运行节点数/local slot、准入 tail/prefix、选定 guard/abort 边界和记录开关。
  容量 6 为 359940 组、容量 8 为 532140 组，最大单步均为 22 runs；最大 run/
  literal 分别为 314/133 和 334/137。探针调用真实生产 builder，使用合成 SRAM
  地址；不证明实际总线延迟、SM 启动偏移或边沿误差。首次容量 6 探针有编译错误，
  首次 checkpoint 有失败 B map 路径错误，均保留原件；修正辅助入口后成功，未改变
  生产图逻辑。原有 active mask、raw 退休/回绕和取消测试仍由前序证据分别证明。
- 当前源码：指纹
  `61dc542b045cc01fe402269b4b8bbdd1c2e1a07c332f834a81cf9f07d40774e8`；容量 6 build
  `20260914080902`，容量 8 build `20260914075453`。两包 SHA 在 `current-plan.json`
  中区分，map 绑定见 `source-checkpoint-r2.json`。当前硬件回归只部署容量 6 到 NO1–NO4；容量 8
  只有目标链接，没有本轮 HIL。未操作 NO5。
- 当前 P3：quick 流程完成，SCK 训练、replay 矩阵和短帧的
  passed/closed_loop_passed/realtime_gate_passed 均通过；STOP 后四板原生 TDMA SD
  一致。`strict_gates_passed=false`，本轮唯一失败项为总时长 513.447 s 超过
  450 s；构建复核为 326.346 s、OTA 为 103.567 s，详见 `p3-r1/timing.json`。
  不能把本轮 SCK 成功外推为间歇失败根因已解决，更不能直接提升正式锁相。
- 自主预采：为保持与前序配置对账，显式使用 `current-plan.json` 绑定的既有诊断
  matrix；没有把它替换成本轮新矩阵。四板各 34 条原生记录完整，无漏采，SD 字节
  一致。NO1 保留 epoch 1、序列 8181–8187 的完整 raw 记录，读取区间均为 252 ns，
  仅为 timer 访问诊断。自主整窗三项判据仍失败，四板各一次真实 missing；预选
  3–6 s 中段接收增量为 917/917/917/918，拒绝、missing 和各相位 start miss/
  overrun 无增长。主板 TDMA RUN 保留峰值为 594.996 µs，不能从此次单窗变化推断
  UI 修复降低了实时 WCET。四板自主 DPLL trace 为零，真实更新路径仍未接通。
- 导出修复：原生 TDMA SD 通过后，主机 raw 副本上传的 BEGIN 和首次 DATA 出现
  应答超时。辅助脚本误用了通用应答过滤器，合法复合应答不能返回；事务状态证明
  BEGIN 和首块 DATA 已执行。改用既有 `storage_file_upload` 完整应答入口，核对
  transaction/path/length/CRC/offset 后续传，两次 SD 读回与主机副本一致。两次失败
  均保留；该文件仍是主机导出后写 SD 的副本，不是板端原生 recorder。没有修改生产
  SCPI 工具或增加超时以掩盖问题。
- 普通恢复：三项短帧判据通过，四板各 26 条记录和 SD 一致；NO1 七个 raw age
  查询均 UNAVAILABLE，旧自主记录没有复活。普通中段仍有主板 TDMA overrun 198、
  VDC start miss 207，不能关闭全表 WCET。最终四板 STOP/config ACK、临时许可证
  inactive。全窗、中段、raw/SD 和失败对照见 `review-final.json`。
- 快速迭代：按用户要求，下一切片先降低 quick 验收自身耗时。优先修复源码扫描器
  遍历历史 `out/` 后才排除的目录开销，保持既有扫描范围和 P3 硬件判据，重新测量
  增量构建与完整 quick 流程；不能以跳过必跑门禁或提高超时门限替代提速。
- 剩余门禁：编译容量 2/3/4/5/7 尚待当前源码目标链接；容量 8 及其他硬件配置尚未
  验收，四板运行不能证明八块物理节点。`VDC-TIME-002` 的完整准入条件继续保留，
  后续才进入 `VDC-TIME-003` 的切换连续性及实际边沿误差界，再进入
  `VDC-TIME-004` 的 trailer/同圈输入。raw timer 读取不授予 COMMON_TIME 或 formal
  qualification；本轮不接 follower command，不据 RAM 修复宣布 DPLL 已锁相。

### VDC-PROGRESS-20260914-010 — 原始计时配置矩阵、重臂与归档退休

- TODO task ID：`VDC-TIME-002/003`、`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`。
- 状态：本次配置/生命周期补测完成；`VDC-TIME-002` 保持 IN PROGRESS，
  `VDC-TIME-003/004` 保持 PENDING，长期目标未完成。
- 日期：2026-09-14。
- 变更：扩展 `test_tdma_origin_raw_time.py`、`tdma_origin_raw_graph.c` 和
  `tdma_origin_record_frozen_cases.c`，未改变生产固件或 PIO 逻辑；测试与当前源码
  P3 凭证提交 `8ece6da`，文档另行提交。下述组合数、容量和时序均为快照，非事实源。
- 证据根：`out/HardwareAcceptance/20260914/dpll-raw-lifecycle/`。`plan.json` 绑定
  前一封存 manifest；`current-plan.json` / `source-checkpoint-r2.json` 绑定当前
  build `20260914071502`，源码指纹
  `0bc3adba3bd2da8a23bd79e485e7aaba32a6ba3e7ae0fcfc6af79feff3d5c700`，package SHA
  `8c11f84ced9724aec9f6f9057cfdf2baba265c6fe8cdcc8407cf8fade4759de4`。
- 配置矩阵：真实生产 builder 编译容量 2–8、各运行节点数的全部有效 active mask/
  local slot，组合 guard 的 7 个边界值、abort 的 3 个值及记录开关，共 124488 组
  构造通过；最大单步为 22 个 run，上限 24。当前容量 6 最大占 314/320 runs、
  133/140 literals；容量 8 最大占 334/352、137/148，详见 `matrix-review.json`。
  稀疏 mask/非零 local 的真实图执行与 mailbox overlay 对账通过。探针固定
  PIO/SM/DMA、地址、prefix 和 padding，不能推断全部几何或实际总线延迟已覆盖。
- 生命周期：生产 STOP/frozen read/persona/invalidate 路径覆盖 88 B raw record
  每个内部复制分割点的退休交错、guard 回绕、published_version 回绕、FAULT 原始
  诊断保留、旧 RTT 格式/epoch 拒绝和 persona 复用后 STOP 不复活旧归档；既有
  admission/build-job 有界取消回归通过。配置/图模型 49 项和生命周期/准入/构造
  15 项通过，完整命令见 `matrix-tests-r1`、`lifecycle-tests-r1`。
- 目标资源：A/B 构建通过，`.data`、`.bss`、heap 地址/大小与上一实现一致，见
  `layout-review.json`；本轮没有新增 RAM。目标链接仍只覆盖当前编译容量，其他
  容量只有 host 矩阵，不能借本轮四板 HIL 关闭全部准入配置的目标验收。
- 当前 P3：四板 OTA 和 quick 流程完成，receipt 为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC；`strict_gates_passed=false`。SCK 训练及
  replay 行选择失败，没有满足飞行重装预算的实测行，完整失败保留于
  `p3-r1/diagnostic.json` / `p3-receipt-r1.json`。后续生命周期采集显式使用
  `current-plan.json` 绑定的既有诊断 matrix，未把旧校准或 quick 完成提升为严格
  校准通过。未操作 NO5。
- 两次自主预采：`resident-r1/r2` 均先普通启动，再有限自主许可，四板每轮各
  34 条 SRAM 记录完整、无漏采。全部 STOP 后原生 TDMA SD 字节一致；NO1 raw
  epoch 从 1 增至 2，各保留连续 7 条完整记录，序列分别为 8176–8182 与
  8122–8128，第二代 timer 晚于第一代。epoch 为本地原始归档代际，不是分布式
  session。FIFO 存在、arm 前 TX CS 为高、计时前后 high/low/high 一致；读取区间
  均为 252 ns，邻圈 arm 间隔分别为 1000.020–1000.636 µs 和
  1000.016–1000.600 µs，不能作为实际边沿偏移、抖动或锁相精度。
- 全窗失败：两轮均保留 `passed/closed_loop_passed/realtime_gate_passed=false`。
  四板每轮各有一次真实 receive_missing，启动/切换的拒绝及超限仍保留；旧
  evaluator 的普通 persona/software TX count 规则也不适用于自主，不能因此
  忽略真实缺失。预选 3–6 s 中段接收增量分别为 917/916/915/917 与
  917/918/916/916，拒绝/缺失以及各相位 start miss/overrun 无增长。中段通过不
  替代全窗，完整对照见 `review-final.json`。
- 耗时边界：两轮自主主板保留的 TDMA RUN 完整峰值为 652.024 µs 与 592.484 µs；
  本轮生产逻辑未变，同一 build 也有峰值变化，不能据此归因于某段代码或宣布
  优化。四板自主 trace 均为零，真实 DPLL 更新 WCET 仍未测得。
- 切换审计：`handoff-review.json` 重新解码上一 build 的原件，只将 missing 定位
  在普通转自主的采样区间，未测出精确停发时长。源码顺序为 MAILBOX → STOP →
  PERSONA → BUILD_BEGIN/BUILD_STEP → SEED → SMS → INSTALL；Core0 在固定上界
  内完成构造，并非每个 label 等一个 Core1 周期。普通服务和自主图共享 workspace
  union，必须证明安全准备与有界交接，不能边跑旧 DMA 边覆盖，也不能压掉 missing。
- 存储与恢复：先完成 TDMA SAVE/释放 StorageAO，再处理 DPLL，零 trace 如实保留。
  每轮 728 B raw 主机导出另存 NO1 SD、两次读回一致，明确区别于板端原生 recorder。
  `restored-r1` 普通短帧三项判据通过，四板各 26 条原生记录与 SD 一致；NO1 的
  raw age 查询全部为 UNAVAILABLE，后续 STOP 未复活旧归档。恢复的中段仍记录
  主板 TDMA overrun 228、VDC start miss 230 和 DPLL start miss 1，三项运输判据
  不代表全表 WCET 通过。最终四板 STOP/config ACK、许可证 inactive；各轮首次
  START 至全部 STOP 无 SCPI 查询采样。
- 主控复核：`audit.py` 从原生 `.bin` 重解码 board/build/epoch/CRC/长度，比较 SD
  字节，核对两代 raw、退休查询、源码/package 和 P3 引用 SHA；保留 quick 严格
  校准失败、自主整窗失败及普通模式超限。软件/P3 凭证与文档分别通过相应提交
  门禁，最终提交身份和证据哈希由本目录 `slice-manifest.json` / `commit-proof.json`
  在提交后封存。
- 下一 gate：`VDC-TIME-002`。补齐其余准入几何/目标容量资源与取消验收；再闭合
  严格校准、`VDC-TIME-003` 的切换连续性及边沿误差界，之后才开放同圈
  trailer/evidence、真实自主 DPLL 更新预算及命令契约后的接线。

### VDC-PROGRESS-20260914-009 — 自主原始计时原型与四板预采

- TODO task ID：`VDC-TIME-002/003`、`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`。
- 状态：原型已实现，`VDC-TIME-002` 保持 IN PROGRESS；全窗计时正式验收及输入接线
  仍为 PENDING，长期锁相目标未完成。
- 日期：2026-09-14。
- 变更：代码与当前源码 P3 凭证提交 `6127da3`。自主 DMA 在既有 TX owner/预留 SM 内
  配置 latch，每圈清 FIFO、重装计数器，使能前后采 Timer1 raw high/low/high；
  boundary 暂停后先判断 FIFO 非空再读取，缺边沿不等待。格式由
  `TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME` 描述，冻结读取与 SCPI 追加原始字段。
  CPU 不在发车路径打时间戳，不改 PIO 指令、wire trailer、DPLL eligibility 或
  COMMON_TIME/formal flag。独占 sniffer lease 内合并冗余禁用写，释放/FAULT 仍关闭。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-raw-time/`。以下容量、计数和
  时序均为快照，非事实源；`plan.json` 绑定前一封存 manifest，`current-plan.json`
  与 `source-checkpoint-r2.json` 绑定本轮源码和 package。
- 资源：真实 builder 编译容量 4/5/6/8，各容量的运行节点矩阵构造及图执行通过；
  探针使用连续 active mask、固定 local slot/guard，不能外推所有配置值。容量 6、
  运行 6 占 314/320 runs、133/140 literals。原始记录由 48 B 增至 88 B；A/B 目标
  workspace 由 7712 B 增至 8120 B，消耗后续对齐 padding 408 B，剩余 72 B。
  总 `.data`/`.bss`、heap 位置不变，heap 外余量仍为 1132 B。不能据此声称新增记录
  零 RAM 成本；其他容量仍需目标配置验收，详见 `graph-matrix.json` / `layout-review.json`。
- 软件与构建：既有 origin 构造/记录/准入、command DMA 和 timestamp clock 回归
  21 项通过；执行真实 DMA 图的模型 20 项通过，覆盖连续发布、序列回绕、完整 raw
  字段、缺 latch、坏 mailbox、缺回传、raw 跨字和旧 FIFO。模型不模拟实际总线/PIO
  延迟。初次缺 include 路径及合成回绕起点错误分别保留于 `graph-model-r1/r2`，修正
  后 `graph-model-r3` 通过；配置变体和新 raw 生命周期的定向组合覆盖尚未全部闭合。
- 当前硬件：build `20260914063601`，源码指纹
  `1ac9b61466444cbf9564b39f6e8384fc7ce7b09c4818227d6ab045bd1fea7e1b`，package SHA
  `246b6b857a77ba38b8701874e01fb64378ebd38037c3371946952716f18d751b`。四板 OTA、
  quick P3/短帧通过，`p3-r1/diagnostic.json` 的 strict gates 通过；凭证范围为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，不含四板正式锁相。未操作 NO5。
- 自主预采：`resident-r1` 先普通启动、再有限自主许可。四板各 34 条 SRAM 记录完整、
  无漏采，STOP 后原生 TDMA SD 字节一致。主板保留连续序列 8169–8175 的 7 条完整
  raw 记录，epoch/首尾 sequence 一致，运输检查和 FIFO 存在成立，arm 前 TX CS 为高；
  Timer1 high/low/high 一致，前后读取区间均为 252 ns，邻圈 arm 间隔为
  999.896–1000.736 µs。该区间及 arm 间隔不是实际边沿误差或锁相精度。
- 全窗失败：原 evaluator 保留 `passed/closed_loop_passed/realtime_gate_passed=false`。
  普通 persona/software TX count 规则不适用于自主 origin，但四板各有一次真实
  receive_missing 增量，切换期间的 reject/调度超限仍需定位；不能因为旧规则不适用
  就放行整窗。预选 3–6 s 窗口四板 UP/DOWN 连续，接收增量为 916/917/917/916，
  拒绝/缺失及各相位 start miss/overrun 无增长。完整窗口与预选窗口同时保留于
  `review-final.json`，不会以裁短窗口消除失败。
- 自主耗时：主板按同状态/配置/许可证保留的 TDMA RUN 完整峰值为 596.672 µs，
  owner 508.796 µs、adapter 383.020 µs、RX parse 189.308 µs、origin publish
  67.828 µs；嵌套区间不能相加。相较上一切片峰值增加 51.628 µs，但这不是受控
  A/B 因果结论，也不是 DPLL 更新 WCET。四板自主 trace 均为零，主板仍为
  diagnostic-only、resolution 为零，不能宣称真实 DPLL 更新预算已满足。
- 存储与恢复：先保存 TDMA/释放 StorageAO，再处理 DPLL，零 trace 如实保存状态。
  raw 的 728 B SCPI 导出另存 NO1 SD 并两次读回一致；这是主机导出的 SD 副本，
  不是板端原生 recorder 文件。初次 BEGIN 应答被通用串口筛选丢弃，后续恢复误把
  active 字段当事务 ID；两次失败、事务读回与正确续传保留于 `resident-raw-sd-r1/r2/r3`
  和对应 JSON，未删除旧文件或重建事务。辅助诊断的未定义查询及错误响应也保留。
  随后 `restored-r1` 普通短帧严格三项通过、四板各 26 条记录和 SD 一致；该恢复流程
  仅验证运输，trace 为零不证明普通 DPLL 更新或锁相。最终四板 STOP/config ACK、
  许可证 inactive，首次 START 至全部 STOP 无 SCPI 查询。
- 主控复核：`audit.py` 从原始 `.bin` 重解码，核对 board/build/epoch/CRC/长度/SD，
  复核 raw 字段、源码指纹、package 和 P3 凭证引用 SHA；失败原件保留。实现提交前
  staged 指纹门禁及 pre-commit 通过；文档另行执行回归门禁并独立提交后封存。
- 下一 gate：先补齐 `VDC-TIME-002` 的非连续 mask/local slot/guard 组合和 raw 的
  STOP/重臂/取消证据，再解决 `VDC-TIME-003` 的切换缺失与边沿偏移/抖动。通过后
  才能执行 `VDC-TIME-004` 同圈 trailer/evidence，继而测真实自主 DPLL 更新 WCET；
  不跳到命令接线或 PI 调参。

### VDC-PROGRESS-20260914-008 — 自主计时事件、资源与区间模型审计

- TODO task ID：`VDC-TIME-001/002`、`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：事件与资源只读审计 DONE；原始计时原型 IN PROGRESS；硬件验收 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-time-audit/`。以下容量、数量和
  模型结果为快照，非事实源；`plan.json` 绑定上一切片封存 manifest 与当前源码指纹。
- 事件顺序：DMA 发车字可能在 PIO guard 期间提前进入 FIFO；control 的 boundary
  token 在 CS 拉高后产生，既不是发车边沿也不是返回 CS。预留 TX latch 程序占位
  仍在，但 `origin_configure_sms()` 未配置/使能；RTT 只保存两类 CS 事件的相对
  倒计数。Timer1 从 `clk_sys` 计数，现有 CPU 读取采用 raw high/low/high 复验。
- 实际 builder：`graph_audit.c` 编译当前生产 builder 并使用实际 workspace 容量，
  编译容量 4/5/6/8、各自允许的运行节点数均构造成功。当前容量 6 下，运行节点
  4/5/6 分别占 293/304/315 个 run、122/124/126 个 literal；实际分配为 320/128，
  因此此探针满节点只余 5 个 run、2 个 literal。构造输入使用连续 active mask、
  local slot 为零及固定 guard/prefix；literal 去重可能随配置值变化，不是本轮 live
  profile 的容量读回。通用 builder 的 384/160 上限不是可用 SRAM；native sizeof
  单独标记，未当作目标 link map。
- 候选操作核算：复用现有 emitter 对 FIFO 清理、重装、Timer1 前后 high/low/high、
  暂停及缺 FIFO 分支计数。独立操作为 18 个 run，其中两次 mask 写替换已有操作，
  净增估算为 16；运行节点 4/5/6 的候选总数为 309/320/331，满节点超出当前分配。
  probe 新用 8 个 literal，完整图的去重与准入仍待复核；不能把此探针当已集成的
  可执行 DMA 图。候选 raw 字段增加 32 B，producer 加归档的布局估算增加 288 B，
  临时副本、对齐和目标链接成本尚未闭合。
- 时基模型：`timer_latch_model.py` 解释当前四条 latch 指令，显式假定 DMA 访问
  顺序、使能区间、GPIO 和启动延迟。2400 组候选中 2269 组区间包含模拟实际边沿，
  131 组跨字不一致被拒绝；直接以先读 timer 加倒计数计算单点，反例偏差达到
  41 个 tick。以上为合成输入，不能换算成实板精度结论；模型证明需要传播误差区间。
- 生命周期边界：模型确认 FIFO 满时首字保留、无边沿不产字、未清旧 FIFO 会读取旧
  值，并区分采样内部跨字回绕和两个一致样本之间的回绕。来源/session/STOP 等
  拒绝条件在模型中仅为抽象输入，不算生产实现负测通过；必须由下一集成切片验证。
- 验证：现有 origin 构造/冻结记录/准入及 timestamp clock 回归 17 项通过，文档
  自回归测试 18 项通过；原件与实际命令分别保存于 `origin-tests-r1` 和
  `docs-tests-r1` 日志。文档检查、pre-commit 与证据复核完成后单独提交和封存。
- 范围与回退：本轮未修改生产固件、PIO、构建、工具或测试，也未操作四板/NO5；
  没有新 build 或新 P3 receipt，不提升上一轮整窗失败及锁相结论。硬件终态仍引用
  上一切片最终 STOP 原件，未将其冒充本轮 live 查询。文档单独更新 TODO 子任务和
  Draft 方案，不改变契约登记状态。
- 下一 gate：`VDC-TIME-002`。先将候选事件记录纳入真实 builder、完整生命周期及
  静态资源预算，完成目标链接；随后按既有流程执行当前源码 P3 和四板原始计时
  验收。时间准入和 trailer 接线属于后续 `VDC-TIME-004`，不能直接开展锁相调参。

### VDC-PROGRESS-20260914-007 — 自主 origin 与 DPLL 输入缺口补测

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`、`VDC-CMD-001`。
- 状态：IN PROGRESS；目标模式的时间输入和全窗口连续性尚未闭合。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-resident-baseline/`。以下计数、时长、
  构建号与测量值均为快照，非架构事实源；`plan.json` 绑定上一切片 manifest。
- 范围修正：前序 `dpll-four-board-profile`、compact RX 与停止验收切片的 DPLL 更新
  对照使用普通 origin，没有启动自主 origin 许可证。其内部 LOCKED、调度迟到及
  命令增量结论仍属于原运行模式，不证明 wire 自主环路中的 DPLL 更新已经实现。
  原先资源回收和短帧结果仍有效，不能因此提升 resident 或锁相任务。
- 当前源码/硬件：沿用已通过 quick P3 的 build `20260914051939`，指纹
  `cfe924fbc3e53593280ccea6ddcefe778d69c0a6e180cb7a9c6cb17b7486aef8`；本轮没有
  修改固件、PIO、构建或生产工具，也没有用新手工报告替换 P3 receipt。
- 实板流程：四板先配置 provisional/clock evidence，普通启动后显式签发有限自主
  origin 许可证并采集；首次 START 到全部 STOP 无 SCPI 查询。`resident-r1`
  完成后全部 STOP/撤销许可证，再保存 TDMA、释放 StorageAO lease，处理 DPLL
  trace。四板各 34 条板端记录完整、无漏采，SRAM/SD 字节一致；DPLL trace 均为
  零条，不生成空样本的锁相结论。随后 `restored-r1` 恢复普通配置并通过短帧/SD。
- 原验收失败保留：自主模式整窗 `passed/closed_loop_passed/realtime_gate_passed`
  为假。NO1 的旧 evaluator 仍要求普通 persona 和软件 TX count 增长；这些不适用于
  自主发车，但不得据此抹掉切换期间真实的 missing 增量，以及 NO4 的短暂 DOWN/
  recovery。`review-final.json` 同时保留原错误及逐板区间计数，未修改验收器放行。
- 固定中间窗口：预选启动后 3–6 s 的原始记录，四板 UP/DOWN 连续、接收/拒绝/丢失
  对账正常；接收增量为 917/916/916/917，拒绝和丢失增量均为零。主板 persona 为
  16、FSM 为 5，自主模式成立；四板 TDMA/VDC/DPLL start miss 与 overrun 均无增长。
  这是定位稳定模式的窄窗口，不替代整窗失败或真实更新 WCET。
- 主板自主计时：STOP 后读取已按自主状态/同配置/同许可证筛选的 `PROFile:RUN?`，
  对应 5221 次自主 service 中保留的完整峰值为 545.044 µs；其中 owner 468.5 µs、
  adapter 358.704 µs、RX handoff 204.412 µs、origin publish 73.228 µs。嵌套区间
  不能相加；该峰值未包含有效 DPLL 更新，不能与普通 origin 的峰值直接归因比较。
- 无输入直接原因：主板中间窗口 timestamp flags 为 diagnostic-only、resolution
  为零。`tdma_pio_spi_ring_origin_invalidate_time()` 清理旧观测；物理 origin RX
  显式返回零边沿时间戳；DMA `L_STAGE` 每圈清零 DPLL trailer。
  `tdma_origin_observation_t` / `tdma_origin_record_t` 是运输/RTT 事实，缺少可用于
  鉴相的绝对边沿时间。DPLL 相位在执行而 trace 不增长，与此路径一致；不能通过
  修改 valid flag、保留旧 observation 或使用 CPU 提取时间伪造输入。
- 验证与复核：`audit.py` 重新解码当前 `.bin`、核对 build/board/epoch/CRC/长度及 SD，
  绑定源文件 SHA，保存全部失败和预选窗口；`resident-r1-stopped-readback.json`
  保留自主峰值和停止后诊断读回。最终四板 STOP/config ACK、许可证 inactive。
  文档按自回归门禁验证后单独提交，原件由 `slice-manifest.json` 封存。
- 下一 gate：先推进 `VDC-TDMA-001` / `VDC-EVID-001` 的自主边沿计时与 trailer
  关联，设计边界已补入 `VDC_COMMAND_TRANSPORT_PLAN.md`；随后才测自主 DPLL
  实际更新成本。命令运输、共同时间和正式输出锁相仍未接通，不以无输入低耗时
  或普通模式 LOCKED 关闭长期目标。

### VDC-PROGRESS-20260914-006 — 有限采集交接验收与资源切片闭合

- TODO task ID：`VDC-RESOURCE-001`、`VDC-SCHED-001`、`VDC-VERIFY-001`。
- 状态：`VDC-RESOURCE-001` 当前编译容量切片 DONE；调度和锁相任务保持 IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-stopped-acceptance/`；以下构建号、
  大小、次数和时长均为本切片快照，非架构事实源。`plan.json` 绑定前序封存 manifest。
- 粗校准调查：TOPOLOGY 使用完整控制响应时限，固件拒绝时可能只发布 SCPI 错误队列，
  原工具的超时不能区分配置拒绝和丢响应。`_control_command()` 现在记录动作起点、
  耗时及失败后一次错误队列读回；不重试动作、不抬高时限、不把超时晋级为成功。
  独立粗校准重复八轮、P0T 后粗校准及软件复位后重跑均通过，本次 P3 也未复现；
  旧失败原件保留，间歇问题根因仍未确认，复发时以 `error_after` 继续定位。
- 交接检查：四板有限采集在全部 STOP 后导出，使用 `validate_tdma_stopped_handoff()`
  复验 START/STOP ACK、板卡集合、配置生效、build/epoch、冻结终态和原始 `.bin` 的
  CRC/长度/采集完整性。proof 与逐板原始字节 SHA256 纳入 receipt；其他模式继续
  原有运行中交接。该 proof 只替换有限采集不适用的 live handoff，不改变收发、
  调度或质量门禁，也不以 summary 缓存中的通过标志代替原始记录。
- 软件与旧证据复核：相关校准/P3/板端记录/启动/TRN-03 回归 234 项通过，包含 C
  recorder 真实输出及缺板、STOP 失败、旧 build、epoch、CRC、长度和漏采负测。
  `previous-handoff-audit.json` 仅离线验证旧字节交接，保留旧 P3 的失败结论；
  当前硬件验收单独执行，未使用 replay 或旧凭证放行。
- 当前源码：build `20260914051939`，1050 文件指纹
  `cfe924fbc3e53593280ccea6ddcefe778d69c0a6e180cb7a9c6cb17b7486aef8`。
  A/B/Boot 构建和链接检查通过；`source-checkpoint-r2.json` 确认 A/B `.data/.bss/heap`
  布局与上一切片完全相同、PIO 头文件字节一致，保留已验证的 1104 B RAM 回收及
  1132 B heap 外余量。本轮只修改验收工具和测试，没有固件或 PIO 改动。
- P3：四板 OTA、软件复位、配置校准和 process-image/FIFO 短帧流程完成；
  `p3-r1/diagnostic.json` 的 `strict_gates_passed=true`、`failures=[]`，STOP 后交接
  proof 验证四板各 14 条记录。约 416 s 的流程在当前配置时限内，凭证范围为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`；该范围通过不等于 full 验收、全表 WCET
  闭合或正式锁相，也不覆盖前序 TOPOLOGY 超时原件。
- 四板对照：一次 `candidate-r1` 显式启用 provisional/clock evidence，随后
  `restored-r1` 恢复普通配置；两轮短帧 passed/closed-loop/realtime gate 通过。
  首次 START 到全部 STOP 之间无 SCPI 查询；先保存 TDMA、释放 StorageAO lease，
  再处理 DPLL。TDMA SRAM/SD 字节相同，DPLL CRC 与重复 SD 读取通过；普通恢复
  没有 DPLL trace 样本，明确记录为空，不作为更新路径或锁相证据。
- 调度结果：`comparison-r1.json` 的实际更新窗口中，主板 DPLL run/start miss/
  overrun 为 2184/645/6，TDMA overrun 为 1371；三从板 DPLL start miss/overrun
  均无增长。普通恢复后四板 DPLL start miss/overrun 均无增长，主板 TDMA overrun
  仍增长 474。该对照支持继续区分真实更新负载与上游迟到，不能用无更新路径或
  稀疏 last-call 峰值关闭静态预算。enabled/quarantined mask 没有新增节点隔离。
- 锁相结果：实际更新每板 76 条 DPLL trace；NO1 全为内部 LOCKED，NO2–NO4 全为
  CHECKING，三从板命令接收和应用增量仍为零。`candidate-r1-lock-review-r2/`
  保留 trace、分析图和 STOP 后读回，仍未证明命令运输、共同时间应用或实际输出锁相。
- 最终复核：`review-final.json` 核对当前源码、包、链接产物、P3 proof、四板原始
  记录、SD、调度与锁相边界；四板最终 STOP、配置 ACK 且临时许可证 inactive。
  代码/凭证与 VDC 文档分离提交，证据由 `slice-manifest.json` 和 `commit-proof.json`
  封存；当前资源切片关闭不改变长期目标 active 状态。
- 下一 gate：`VDC-SCHED-001`。先分解主板上游迟到和 DPLL 实际更新成本，再复核
  `VDC-ROLE-001`；命令契约仍为 Draft，运输接线必须等待阶段基础与独立审核。

### VDC-PROGRESS-20260914-005 — compact RX 专用状态与停止后导出

- TODO task ID：`VDC-RESOURCE-001`、`VDC-SCHED-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS；资源代码、目标链接和四板短帧已复核，严格 P3 尚未闭合。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-compact-rx-state/`；以下大小、次数与
  构建号均为本切片快照，非架构事实源。`plan.json` 绑定前序审计封存 manifest。
- 实现：仅将 resident compact DELTA 的实例改为 `refmem_sync_delta_context_t`，
  保留 peer、mirror 和本地 quality，移除该实例未使用的 ACK/fence/remote-quality。
  栈上私有 view 复用原接收校验与排序逻辑；通用 receiver 保留全部维护能力，wire
  不变。有效非 DELTA 输入在修改 peer/mirror 前以 BAD_TYPE 拒绝，不使用布局强转、
  动态分配或额外静态指针。旧 DELTA payload 接受及计数语义保持，不混入协议修复。
- 行为对照：旧 HEAD 接收器独立编译后，与当前通用和 compact receiver 对照；容量
  4/5/6 每种运行 16384 组通用输入与 2341 组 DELTA 输入，比较逻辑 snapshot、全部
  通用 context 及 compact 的 peer/mirror/quality，通过；含序列回绕、CRC、截断、
  错误身份、重复、stale、gap、NULL 和重置。现有及新增 C unit tests 通过，原件见
  `differential-results.json`、`refmem-new-tests-r1.log`。
- 接口回归：相关 Python 首轮 159 项通过、ARM 桩函数测试失败；桩函数仍沿用动态
  节点改造前的无参签名和缺失 staged config 的调用次数预期。同步测试桩后通过；
  两次失败与最终通过分别保留，不改变 ARM 生产代码。
- 目标链接：A/B/Boot 构建和 Flash link checks 通过。`source-checkpoint-r2.json`
  绑定 build `20260914044559` 及当前源码；A/B 的 compact 实例由 2140 B 降到
  1036 B，`.bss` 减少 1104 B，heap 外余量由 28 B 增至 1132 B；`.data`、heap
  保留量和 PIO 字节未变。容量 4/5 的 736/920 B 仍仅为 host sizeof 对照，未称为
  对应目标固件的 RAM 验收。
- 采集约束修复：标准短帧工具原先在记录窗口结束后、RING 仍运行时导出。有限采集
  现在在首次 START 后通过 finally 尝试全部 STOP，再读取冻结记录；START 或 STOP
  失败保留各板原始动作，不把失败导出为成功样本。四板 quick P3 不再请求
  `--leave-running`；原有运行中交接门禁仍保留，停止导出不能冒充该门禁通过。
  对应短帧/P3 软件回归 164 项通过，含多板顺序、启动失败和停止失败注入。
- P3：当前源码四板 OTA、软件复位及短帧诊断流程完成，源码指纹为
  `fdc2e509bf6565a375524ca7ad3061febcc77c55c9f2870bdbf69289faa69f67`。
  `p3-r1/diagnostic.json` 保留 NO1 粗校准 TOPOLOGY 超时，以及停止导出导致未交接
  运行中环路两项失败；`strict_gates_passed=false`，凭证只属于四板 QUICK_DIAGNOSTIC。
  本轮总流程约 334 s，在配置时限内；不能因此覆盖前序超时或宣称严格验收通过。
- 四板对照：`candidate-r1/r2` 使用相同 pinned matrix 启用真实 DPLL 更新，随后
  `restored-r1` 恢复普通配置；三轮均 `passed/closed_loop_passed/realtime_gate_passed`
  为真。各轮记录均在全部 STOP 后导出，先保存 TDMA 释放 StorageAO，再保存 DPLL；
  TDMA SRAM/SD 字节一致、DPLL CRC 解码及重复 SD 读取通过，命令记录中没有运行窗口
  查询。标准 P3 的板端短帧记录也在全部 STOP 后导出并保存 SD。
- compact 接收：两轮四板接收增量分别为 4696/4700/3132/1570 和
  4704/4709/3140/1572，拒绝及坏 mailbox 增量均为零。这些停止读回差值包含配置过程，
  不能当作稳态包率；板端没有直接导出每个 peer/mirror/quality，存储行为等价性由
  独立 host 对照证明。原始读回见各轮 `*-compact.json`。
- 调度与锁相：同一板端稳定窗口内，四板 DPLL overrun 增量为 10/0/1/0 和 9/0/0/0；
  主板 DPLL start miss 为 642/635，TDMA overrun 为 1343/1374，仍未闭合全表 WCET。
  enabled/quarantined mask 保持前序值，未新增健康节点隔离。两轮每板各 76 条 trace，
  NO1 均为内部 LOCKED，三从板仍为 CHECKING，命令接收和应用增量仍为零；本次 RAM
  回收没有补齐命令路径，不能宣称四板锁相。
- 最终复核：`review-final.json` 汇总当前源码、目标布局、短帧、调度与锁相边界。
  四板恢复 STOP、配置 ACK 且临时许可证 inactive；代码/凭证与 TODO/进度分离提交。
- 下一 gate：资源任务保持 IN PROGRESS，先解决严格配置/校准及符合停止采样方式的
  验收交接缺口，再闭合 `VDC-SCHED-001`、`VDC-ROLE-001`。命令接线仍须等待阶段基础
  与契约独立审核，不能将 QUICK_DIAGNOSTIC 凭证等同于目标完成。

### VDC-PROGRESS-20260914-004 — 命令时间域反例与固定邮箱候选

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-004`、`VDC-RESOURCE-001`。
- 状态：IN PROGRESS；契约未冻结，固件和硬件验收状态不提升。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-command-contract-audit/`。
  `audit-results.json` 绑定当前 HEAD、相关源码 SHA256、提取的函数体和前序封存 manifest。
  以下计数/容量是本轮快照，非架构事实源。
- 原函数 host 反例：在编译容量 4/5/6 下运行当前 manager 消费函数体，每种配置包含
  容量探针与 7 个行为案例。同时间域到期/未来正例成立；uptime 领先时提前尝试应用、
  落后时错过已到期命令、过期无年龄上限，以及最大序列回绕到 1 被跳过均复现。
  getter、当前时间和 Domain apply 为受控 stub，结果是 manager 的应用尝试，不是
  新的板端应用或锁相证据。当前 clock conversion 原函数的 identity model 仍保留
  异步启动 epoch 差，不能仅靠调用转换 API 完成共同时间初始化。
- 时间锚反例：对 adapter 的 `sequence * cycle_period + reference_tx_phase` 算式，
  连续 reference latch 的合成输入在 phase 回绕处产生一个 nominal period 的增量差。
  这是算术反例，不是新采样的硬件故障；它不否定该字段用于关联，但阻止把关联标签
  直接当作与物理时间等速的共同绝对时钟。
- 已有 RefMem host 回归通过。首个编译命令遗漏 OTA CRC header include，失败命令
  和 stderr 保留于 `refmem-compile.json`；按既有测试脚本补 include 后在
  `refmem-compile-r2.json` 及 `refmem-run.json` 记录成功，不覆盖原始失败。
- RAM 审计：compact 路径只生成 DELTA，context 对外只提供 peer、mirror、quality。
  `resource-probe-r1.json` 记录容量 4/5/6 中未使用的 ACK/fence/remote-quality 数组
  分别占 736/920/1104 B。它们是 native sizeof 与调用点审计，尚未移除，不能称为
  target RAM 已释放；下一切片必须用原/新行为对照、目标 link map 和 P3 验证。
- 方案：`VDC_COMMAND_TRANSPORT_PLAN.md` 将 local-to-common 映射与主机信号模型分开，
  提出未来 sequence 的 latch reservation、完整时间锚和固定邮箱记录分片候选；
  MASTER 与 FOLLOWER 都须按共同时间提交，不能发送已应用快照后声称同步提交。
  session fence、记录字段和提前量待审核，未登记契约、未启用 parser 或实时 apply。
- 验证：本轮文档门禁结果另存证据根的 `docs-*-r1.log`；没有运行 OTA、板端采样或
  新 P3。此前短帧/调度/正式锁相失败保持原结论。
- 下一 gate：`VDC-RESOURCE-001` 先落实 compact 状态回收的行为等价性及当前源码
  build/P3，再继续阶段基础和 `VDC-CMD-001` 可执行规格/独立审核。

### VDC-PROGRESS-20260914-003 — 锁相长期目标与分阶段执行清单

- TODO task ID：`VDC-LONGTERM-001`、`VDC-SCHED-001`、`VDC-ROLE-001/002`、
  `VDC-CMD-001`、`VDC-CONFIG-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS；本次只更新执行计划和审计索引，不提升固件或锁相验收状态。
- 日期：2026-09-14。
- 变更：按用户要求将长期目标拆为调度/角色基础、命令契约、稳定运输、共同时间应用、
  四板锁相优化、恢复/配置/长稳阶段；保留既有 Task ID，将 `VDC-ROLE-002` 细分为
  `VDC-CMD-001` 至 `VDC-CMD-005`，新增 `VDC-CONFIG-001` 跟踪配置矩阵验收。
  明确审计可先行而接线须等前置 gate；长期分段观测作为扩展，不阻塞当前短时验证。
- 基线证据：沿用 `VDC-PROGRESS-20260914-002` 的封存四板记录和
  `out/HardwareAcceptance/20260914/dpll-four-board-profile/transport-audit.json`。
  本次没有运行硬件采集或生成新的 P3 凭证。
- 源码审计：`distributed_refmem_tdma_flight_parse_mailbox()` 将 VDC 字段写入
  `last_vdc_*` 诊断状态，manager 从独立的 `s_vdc_command_context` 读取命令。
  `distributed_refmem_get_vdc_follower_command()` 使用裸结构复制，需在
  `VDC-CMD-002` 覆盖发布和 reset 的一致性交接；本轮未以并发实测宣称发生撕裂。
- 时间与序列审计：`vdc_dpll_manager_consume_follower_command()` 将
  `effective_vdc_time_ns` 与 `vdc_dpll_manager_now_ns()` 比较，并使用普通大小比较
  跳过命令序列；共同时间映射和回绕语义分别纳入 `VDC-CMD-004` 与
  `VDC-CMD-001/003`。这两项不是三从板零接收增量的实测原因，不能混同运输缺口。
- 验证记录：`out/doc-audit/20260914-vdc-lock-todo/` 保存本次文档检查命令与结果。
  TODO 本身不冻结新 wire 契约；Architecture 和登记表状态不因本次计划更新而改变。
- 下一 gate：完成 `VDC-CMD-001` 的编码/时间域/资源预算及正反测试方案，独立审核后
  再按 TODO 的前置条件实施；调度、角色及正式 evidence 缺口继续保留。

### VDC-PROGRESS-20260914-002 — 四板锁相复测与 owner 读取成本

- TODO task ID：`VDC-SCHED-001`、`VDC-ROLE-002`、`VDC-LOCK-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-four-board-profile/`；下述记录数、
  耗时和构建资源均为本切片快照，非架构事实源。`plan.json` 绑定前序封存 manifest、
  基线源码和固件；原目录保持封存。
- 锁相基线：`baseline-r1` 四板各冻结 76 条 SRAM 记录，先保存 TDMA 记录并释放
  StorageAO lease，再保存 DPLL trace。四板 SD 的 CRC 解码和重复读取字节核对通过。
  NO1 在约 590 ms 的记录内全部为 `VDC_DOMAIN_LOCK_LOCKED`，内部残差为 -9 至
  9 ns；NO2--NO4 全部为 `VDC_DOMAIN_LOCK_CHECKING`，三从板命令接收和应用增量
  均为零。NO1 缺失 bias generation，离线工具另按 update_seq 步长标记
  `decimated_trace`；该计数还包含域内部状态更新，不能据此单独断言采样丢失。
  离线结果仍为 `not_proven`；
  内部状态为 LOCKED 不等于四板锁相、可信 corrected jitter 或正式同步验收通过。
  原件与图见 `baseline-r1-lock-review-r2/`。
- 调度基线：`baseline-analysis.json` 使用板端稳定区间的计数差；四板 DPLL
  overrun 增量为 232/96/96/63，主板 start miss 为 606。稀疏 last-call 采样不能
  用作每次调用分布或各分支 WCET，累计 max 也不能用作本窗口峰值。
- 优化边界：`vdc_dpll_manager_consume_follower_command()` 改为读取 Core1 已拥有的
  active control profile 与 local slot，移除每次整份域快照复制；Core0 的 guarded
  published snapshot、角色/代际应用顺序、命令校验、PI 和静态预算保持原有语义。
  临时 C harness 对旧、新实际函数体运行 1024 组边界组合，逻辑字段结果一致；
  本配置整份快照为 1384 B。相关 Python/host 回归和 Domain C harness 通过。
- 构建拒绝：首次优化触发编译器内联，SRAM service 增长将后续 DMA BSS 对齐到
  下一页，链接超出 RAM；保留 `build-command-r1.log` 和失败 map。用 `noinline`
  保留原 Flash 调用边界，不通过缩减记录器或改调度预算解决该拒绝。
- 构建复核：A/B/Boot 及 Flash link checks 通过，build 为 `20260914025954`，
  源码指纹为 `12a75cdf4252b1dda0f96430c37907c33e7d924cad13498519186ba40bd2dff5`。
  `source-checkpoint-r2.json` 与 `layout-review.json` 证明静态 RAM 和 PIO 字节保持
  原值；函数局部栈分配由 1500 B 降到 112 B，heap 外余量仍为 28 B。
- P3：`p3-r1` 完成当前源码四板 OTA、软件复位、校准与短帧诊断流程；
  `strict_gates_passed=false`。粗校准中 NO2 的 TOPOLOGY 命令超时，且包含构建
  扫描的总流程超过配置时限；两项失败均保留于 `diagnostic.json`，凭证范围为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，不得称为严格 P3 通过。控制配置拒绝并非
  仅见于前序 NO3，本切片未将其归因于单板硬件或声明已修复。
- 四板对照：`optimized-r1/r2` 都完成真实更新、短帧闭环和 STOP 后 SD 字节核对。
  相同稳定区间内，四板 DPLL overrun 从基线 232/96/96/63 降为 11/1/0/0 和
  8/1/0/0；两个优化区间的 deadline 增量分别与 overrun 相同。主板 start miss
  仍为 619/627，主板 TDMA overrun 仍为 1325/1323；未新增负载隔离，不能据此
  关闭全表 WCET。稀疏 last-call 中位数分别由基线 82.066/81.322/77.776/60.036 us
  降至首轮 69.572/52.264/65.334/41.014 us、次轮 59.182/47.548/70.938/42.650 us，
  这些值仅描述被采样调用，不代表 prepare/servo/finalize/publish 的独立分布。
- 优化后锁相：两轮每板各 76 条原始 trace；NO1 全部为内部 LOCKED，NO2--NO4
  全部为 CHECKING，三从板命令接收和应用增量仍为零。只读观测并未被性能优化
  转换为控制命令或正式锁相证据。`review-final.json` 统一复核基线、两轮对照、
  编译布局与 P3 原始失败。
- 恢复：`restored-r1` 恢复普通短帧模式并通过闭环，四板 DPLL 稳定区间 overrun
  和 deadline 增量为零，但没有新 trace 更新，不能用此结果代替真实更新门禁。
  最终四板 STOP/config ACK、临时许可证 inactive；两类记录已顺序保存并核对。
- 命令路径审计：`transport-audit.json` 证明 resident mailbox 的 VDC 字段目前
  进入 `last_vdc_*` 诊断字段，而 DPLL 的 getter 读取独立、按来源保留的命令区；
  既有命令接收路径仍使用 RefMem window intent。本切片不将诊断字段直接当命令，
  也未证明所有 RX stall 的根因；来源、序列、代际和共同生效时间仍须在
  `VDC-ROLE-002` 闭环。
- 原始工具失败：首次锁相汇总读取了错误 JSON 字段名，保留失败输出并在新目录
  重建报告；首次等价 harness 比较 C struct padding 导致失败，后续改为比较所有
  逻辑字段，旧/新函数体和两次输出均保留。这些是报告/harness 失败，不改判为
  板端丢记录或命令行为差异。
- 下一 gate：`VDC-SCHED-001` 保持未关闭，继续分解主板真实更新的剩余超限和
  上游 TDMA 迟到；`VDC-ROLE-002` 优先闭合 resident mailbox 到来源命令区的
  语义交接，并单独复核控制配置超时。正式锁相 gate 保持未关闭。

### VDC-PROGRESS-20260914-001 — DPLL 静态相位入口余量

- TODO task ID：`VDC-SCHED-001`、`VDC-ROLE-001`、`VDC-ROLE-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-phase-admission/`；下述板端计数和
  构建数值均为该切片快照，非架构事实源。前序 TDMA 切片的 manifest 由本目录
  `scope.json` 按 SHA 引用，原目录保持封存。
- 原因：默认完整表的 DPLL 窗口恰好等于 WCET，`app_realtime_run_phase()` 只在
  计数器恰好命中起点时容纳完整 WCET。基线 `baseline-r3` 的四板普通短帧闭环通过，
  DPLL 全窗执行比例仅约 3.5%--6.4%，并无新 TRACE 更新；原始板端记录已 STOP 后
  保存 SD，读回与 RAM 导出逐字节相同。历史对照见 `historical-dpll-review.json`。
- 变更：`PROJECT_CORE1_DPLL_ENTRY_MARGIN_CYCLES` 为全部离散静态表声明入口余量；
  DPLL WCET 和其他执行相位宽度/WCET 保留，DPLL 后续相位整体平移，尾部 guard
  仍为空闲。未修改 runtime 准入检查、PI 参数、主从应用语义、wire 或 PIO/DMA。
- 软件：相关 Python/host 回归 66 项及 VDC Domain C harness 通过；四档完整表均
  通过目录闭合检查。A/B/Boot 与 Flash link checks 通过，build 为
  `20260914022144`，源码指纹为
  `baf58ba35b89cef7122b2727abbdf6042bbeb615cbe7db9f91d9f58bfc64539f`。
  `source-checkpoint-r1.json` 证明静态 RAM 没有增加，原 heap 外余量快照仍为 28 B。
  同一目录回归以旧配置编译时，明确在 DPLL 入口余量断言失败。
- 板端调度：`normal-r1` 与 `restored-r1` 均完成普通短帧闭环；稳定区间的主站
  DPLL 执行比例分别为 99.58% 和 98.98%，三个从站均为 100%，这些区间 DPLL 的
  overrun/deadline 增量均为零。基线相同区间为约 3.11%--5.94%。主站仍有
  start miss，TDMA phase 本身也存在 overrun/deadline；不能把短帧工具的
  `realtime_gate_passed` 布尔解释为全表 WCET 已通过。
- 真实更新路径：`provisional-r1` 显式启用既有调试 observation，短帧功能闭环
  通过，四板各冻结 76 条 DPLL 记录。记录跨度约 503--599 ms，仅覆盖该有限
  TRACE 区间；NO3 含一条 follower state transition，其余为本地观测，不存在
  follower applied command。三台从机的命令 apply/接收进展增量均为零，bias
  generation 仍缺失。SD 数据完成 CRC 解码及重复读取字节核对，原始数据位于
  `provisional-r1-dpll-sd`，离线诊断图位于 `provisional-dpll-analysis/plots`。
- 更新相位的剩余成本：同轮板端调度稳定区间内，NO1--NO4 的 DPLL overrun
  增量依次为 216/107/85/80，deadline 增量为 211/104/83/75；主站 DPLL 执行
  比例降为 78.47%，三个从站仍为 100%。调试负载未新增隔离，TDMA 节点继续
  收发；仍须拆分真实更新分支并闭合当前 WCET，不能继续以无更新路径代替验收。
- P3：`p3-r1` 在 NO3 MARK preparation 的 TOPOLOGY 配置被拒绝后中断；完整
  软件复位重验 `p3-r2` 完成四板校准到短帧流程，但粗校准中的 NO3 TOPOLOGY
  超时仍保留在 `diagnostic.json`，`strict_gates_passed=false`。本切片使用当前
  源码的 QUICK_DIAGNOSTIC 凭证，不宣称严格 P3、完整实时预算或正式锁定通过。
- 周期/恢复：四板对所有已编译周期目录逐项返回完整静态表及 generation ACK；
  最终恢复默认周期，STOP/config ACK、inactive 临时许可证和 SD 保存核对通过。
  `review-final.json` 是原始证据复核入口，最终 manifest 与提交回执分别封存。
- 原始拒绝：`baseline-r1` 的临时采样脚本误将复合 TRACE ARM ACK 视为单字段；
  `baseline-r2` 未先保存取消的 TDMA 记录而遭重 ARM 拒绝。取消记录按原 reason
  导出至 `baseline-r1-recovered` 并保存 SD 后才执行新基线，原失败未覆盖或改判。
- 下一 gate：`VDC-SCHED-001` 保持 IN PROGRESS，先拆清并约束真实更新分支的
  prepare/servo/finalize/publish 成本及主站继承迟到，复核 NO3 控制配置拒绝，
  再推进 `VDC-ROLE-002` 的来源/序号/共同生效时间闭环。只读审计
  `followup-audit.json` 指向 manager 的 uptime 比较、
  非回绕安全序号过滤和 RefMem window intent 接线；它们尚未修改，也不能据此
  宣称已解释所有命令缺失。调度恢复不等于 peer command apply、可信 jitter 或 formal lock。

### VDC-PROGRESS-20260910-012 — P3 phase-domain finding and fail-closed admission

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B、`VDC-ROLE-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 证据：`out/HardwareAcceptance/20260910/p3-094720/dpll-no1-4-internal/` 的原始 capture
  显示 NO1 是 self-loop source/reference，而 NO2--NO4 将远端 origin phase 与各自 raw
  RX counter phase 相减。该目录是单次采集快照，不是稳定性能事实源；重分析产物位于
  `out/pytest/p3-094720-observation-reanalysis/`。
- 结论：NO1 的 ns 级 raw spread 与从机 us 级 raw spread 不是同一已证明物理量。三台
  FOLLOWER 在该快照中没有成功应用 peer command，且 active path 缺 bias generation；
  因此没有任一节点可报告可信 output jitter、corrected residual 或 formal lock。
- 变更：TDMA observation 现在标识 same-clock、common-mapped 或 raw-local phase domain。
  MASTER 遇到跨板 raw local phase 以 `VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED` fail-closed；
  FOLLOWER 继续记录同一 observation，但只旁路 PI/DCO/local promotion。离线报告将
  `raw_jitter_*` 和可信 `jitter_*` 分开，未对齐或 generation 不完整时可信值为空。
- 验证：VDC domain、TDMA adapter host C tests 与 DPLL observation/decode/residual/waveform
  Python regressions 已通过；本 checkpoint 不替代当前源码 P3/HIL。
- 下一 gate：完成 `VDC-ROLE-002` 的 RefMem command receive 闭环，并由 hardware output
  observation owner 发布 generation-bound local-to-common mapping；随后执行同窗 NO1--NO4/
  NO5、role matrix、fault injection 和长期观测。

### VDC-PROGRESS-20260910-011 — dual-observer algorithm remediation objective

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A--D。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期任务目标：把 NO1--NO4 的内部 DPLL 观测和 NO5 的外部波形观测统一为一个可审计、
  可重放、可比较的测量算法。两类算法当前都视为未证明正确；在物理边沿、共同时间锚、
  有向路径和质量准入闭合前，任何散点、微秒级偏差或单节点 ns 级曲线都不能解释为真实
  抖动、锁相成功或失锁。
- 统一样本必须能追溯 `source_slot_id`/`reference_slot_id`、自身 TX/RX 边沿、TDMA
  `sample_sequence`、共同绝对生效时间、segment continuity、CRC/调度结果以及
  `delay_generation`/`bias_generation`。MASTER 和 FOLLOWER 使用同一观测算法；FOLLOWER
  只旁路 PI、积分器、DCO 和本地 lock promotion，不得用主机命令应用记录替代自身相位观测。
- 分阶段交付：
  1. 审计并修正内部/外部 evidence 的物理方向，确保每个节点配对自身发出与自身接收的
     同一边沿；明确 `source`、`reference` 与 TDMA 反向数据路径，禁止把参考节点 TX 到
     本地 RX 当作自身环路观测。
  2. 以 TDMA correlated sequence 及共同绝对生效时间建立跨节点时间锚，按 active
     有向 delay/bias generation 做扣除；禁止用接收时刻、本地重建 cycle 或零默认值对齐。
  3. 建立 fail-closed admission：坏帧、CRC/调度错误、来源错误、序列缺口、segment drop、
     stale、generation 不一致和外部线缆不完整样本只保留 raw diagnostic，并单独统计覆盖率。
  4. 在同一窗口分别计算 raw phase、固定 path bias、transport-corrected residual、真实
     jitter、频率斜率、命令应用和置信度；NO5 只能做同窗只读相关，不能驱动 DPLL 或提升 lock。
  5. 用 host/C、故障注入、`1M3F`/`2M2F`/`3M1F`、主机切换、当前源码指纹 P3/HIL 和长期
     soak 验证；不通过时保留失败证据，不扩大锁定门限或使用旧 receipt/replay。
- 完成定义：NO1--NO4 与 NO5 对同一物理量给出一致的字段和质量语义；每个有效样本可由
  原始边沿重放并定位到 source/sequence/segment；不完整窗口不会生成 corrected jitter、
  `LOCKED` 或 `FORMAL_LOCKED`；主从角色差异只体现在 PI/DCO 控制权。
- 当前边界：最近源码 P3 `out/HardwareAcceptance/20260910/p3-082217/` 的
  `strict_gates_passed=false`，且 TDMA ARM/拓扑/coded-marker 前置失败，不能作为算法
  正确性或锁相证据。下一 gate 是完成 C 端 TX/RX evidence 生命周期审计，再补 admission
  和同窗关联测试。

### VDC-PROGRESS-20260910-007 - unified observation algorithm kickoff

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A/B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期目标：建立一套对 NO1--NO4 内部 DPLL 和 NO5 外部波形都适用的观测定义，
  以“同一 source/reference 有向路径、同一 delay/bias generation、同一 sample
  sequence 和同一共同绝对生效时间”为前提，分别输出 raw phase、transport-corrected
  residual、固定 bias、真实 jitter、频率斜率、命令应用和覆盖率；FOLLOWER 与 MASTER
  使用相同观测路径，FOLLOWER 只旁路 PI/DCO/本地 lock promotion。
- 当前问题陈述：已有 P3 诊断中 NO1--NO4 的内部曲线和 NO5 外部曲线量级不一致，现阶段
  不能把差异解释为真实节点抖动或锁相失败。优先排查内部各节点是否都在测量自身 TX
  到自身 RX 的同一边沿对、NO5 是否使用同窗外部边沿、共同时间锚是否来自 TDMA
  correlated sequence，以及有向反向 delay/bias 是否被正确扣除；坏帧、序列缺口、
  segment drop 和 generation mismatch 必须只进入诊断统计。
- 现有证据边界：当前源码 P3 证据目录为
  `out/HardwareAcceptance/20260910/p3-075811/`；该轮 `strict_gates_passed=false`，
  且报告尚未形成完整 metadata/generation/continuity 证据，因此不能证明内部或外部
  算法正确，也不能宣称 `LOCKED`/`FORMAL_LOCKED`。
- 下一 gate：逐节点核对 C 端 reference/local timestamp 的物理方向和 owner，补齐
  sequence/segment continuity、坏帧和缺口 admission，再用同一窗口比较 NO1--NO4 与
  NO5；完成前不调整锁相判定门限、不用接收时刻重建共同时间，也不以从机不调 PI 为
  理由减少观测点。

### VDC-PROGRESS-20260910-008 — observation provenance capture schema 5

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：将 TDMA correlation flags、reference/local phase、common effective time 及
  双方 logical observation time 从 ring observation 传播到 VDC timestamp evidence、
  DPLL state 和内部 maintenance capture。MASTER/FOLLOWER 的本地观测记录现在可用
  同一 provenance 重放；FOLLOWER 仍只旁路 PI/DCO/本地 lock promotion。
- Capture：DPLL capture schema 升为 5，记录从 64 字节扩展为 100 字节；为保持现有
  8 KiB 文件和固件 RAM 预算，maintenance capture 上限调整为
  `VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES`（当前 76），不改变 TDMA 短帧或实时队列。
- 软件验证：`test_dpll_observation_decode.py` schema 1--5 兼容回归通过（8 项）；
  DPLL capture/residual/waveform 相关 Python 回归通过（62 项）；release 双镜像构建、
  flash-link contract 和 RAM 链接检查通过。
- 边界：schema 5 只完善可重放 provenance，尚未完成 NO1--NO4 与 NO5 的同窗/segment
  continuity 关联、坏帧排除和 formal lock 规则；任何 P3 结果仍不能宣称锁相成功。
- 下一 gate：补齐 schema 5 的长期窗口关联与坏帧/缺口 admission，随后执行当前源码
  指纹下 P3/HIL 和长期观测，不得使用旧 receipt 或诊断 replay 替代。

### VDC-PROGRESS-20260910-009 — schema 5 current-source P3 diagnostic

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 当前源码指纹下 P3 quick diagnostic 已完成，build `20260909235818`，证据目录为
  `out/HardwareAcceptance/20260910/p3-075811/`。流程 `passed=true`，但
  `strict_gates_passed=false`。
- 失败事实：coarse CLK ARM 被拒、coded marker gate 未通过、TDMA closed loop 返回失败，
  NO5 仍为 `insufficient_stable_circular_span_windows`。内部 NO1--NO4 capture 已执行，
  但报告中 NO2--NO4 仍只有单点，不能作为多点收敛或 formal lock 证据。
- TDMA 接收质量在该轮未出现持续坏帧扩散；启动阶段仍记录已有的单次 transport bad/
  process reject，必须与观测缺口分开归因。该轮 P3 只证明 schema 5 固件和观测流程可运行，
  不证明内部/外部算法或跨板锁相正确。
- 下一 gate：继续实现同窗 sequence/segment continuity 与坏帧 admission，优先让
  NO2--NO4 获得与 NO1 相同语义的多点本地 TX/RX 观测，再重复 P3/HIL。

### VDC-PROGRESS-20260910-010 — generation admission hardening

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A/B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：离线 residual analyzer 现在要求每个可修正样本同时携带正值
  `delay_generation` 和 `bias_generation`；缺失、只存在一个、非整数、非正值或跨样本
  generation 变化时，样本保留 raw diagnostic，整段 `delay_correction_available=false`
  且 `transport_corrected_sample_count=0`。路径/方向校验仍独立保留具体拒绝原因。
- 软件验证：观测工具回归及 schema/capture/waveform 相关 Python 测试共 `65 passed`；
  `py_compile` 通过。新增覆盖缺失 generation、部分 generation 和 generation 漂移。
- 当前源码指纹下 P3 quick diagnostic 已完成，build `20260910002225`，证据目录为
  `out/HardwareAcceptance/20260910/p3-082217/`；流程 `passed=true`，但
  `strict_gates_passed=false`。原始失败事实为 coarse CLK topology readback mismatch、
  coded marker gate、2BD5090FE009FA2A 的 TDMA ARM/运行交接失败，以及由此导致内部
  DPLL/NO5 无有效 TDMA 前置窗口；不能据此判断 generation 算法或锁相状态。
- 下一 gate：在不改变 TDMA 短帧和 Calibration training 的前提下，补齐
  source/reference、sample sequence、segment continuity 和坏帧 admission，再用有效
  多窗口验证 NO1--NO4 与 NO5 的同窗 corrected residual。

### VDC-PROGRESS-20260910-001 — unified observation algorithm baseline

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 目标：统一 NO1--NO4 内部 DPLL 与 NO5 外部观测的 residual 语义，明确有向路径、
  固定 bias、真实 jitter、控制命令应用和观测缺口的边界。MASTER/FOLLOWER 共用观测
  计算；FOLLOWER 仅旁路 PI/DCO/本地 lock promotion。
- 已完成：`tools/dpll_residual_analyze/dpll_residual_analyze.py` 现在只在 path/delay
  元数据完整且方向一致时生成 transport-corrected residual；缺失、负值或方向不匹配
  时保留 raw residual，并输出 `delay_correction_available`、修正样本数和拒绝原因。
  path bias 不再把缺失字段静默归入 `0 -> 0`。
- 软件验证：`python -m pytest tests/python/test_dpll_residual_analyze.py -p no:cacheprovider`
  通过，`13 passed`；覆盖缺失 delay、错误方向、固定 delay 改变不影响 corrected
  jitter、命令应用不冒充 local residual 和 follower 元数据缺失。
- 当前源码指纹下 P3：`python tools/hardware_acceptance/p3_hardware_acceptance.py run`
  完成 quick diagnostic flow；证据目录为
  `out/HardwareAcceptance/20260910/p3-052612/`。TDMA、Calibration、内部 NO1--NO4
  观测和 NO5 观测流程均执行完成，但 receipt 的 `strict_gates_passed=false`，NO5
  失败为 `insufficient_stable_circular_span_windows`。该结果证明验收链路可运行，
  不证明 NO5 同窗关联或 `FORMAL_LOCKED`。
- 工具边界修正后的再次 P3 尝试使用 build `20260909213559`，在 Latency Cal 前置阶段
  停止：NO1 calibration profile apply 回读 `active_level=0`，其余三板为请求 level，
  因此没有进入 DPLL/NO5 算法验收。该硬件前置失败不能作为算法回归结论，需在下一次
  P3 前先恢复四板 calibration profile 一致性。
- 最新当前源码 P3 使用 build `20260909214240`，完整执行到内部/NO5 观测；flow
  `passed=true`，但 `strict_gates_passed=false`。失败事实包括一板 coarse CLK ARM
  被拒、coded marker gate 未通过，以及 NO5 的
  `source_dma_or_latch_dropped_records`、`source_dropped_records` 和
  `insufficient_stable_circular_span_windows`。证据目录为
  `out/HardwareAcceptance/20260910/p3-054234/`；该结果不能宣称正式锁相，且说明
  观测丢样与窗口完整性必须和 DPLL 控制状态分开判定。
- 边界：尚未完成 C 端 `reference_tx_phase`/`local_rx_phase` 的物理方向和共同绝对
  生效时间审计；尚未建立 NO1--NO4 与 NO5 的同窗关联，也不宣称板端锁相或
  `FORMAL_LOCKED`。
- 下一 gate：完成 `VDC-OBS-ALG-001` 阶段 B，审计并补齐 C 端 sequence、source/reference、
  delay generation 和共同时间锚字段，再运行相关 host/C 回归和当前源码指纹下 P3。

### VDC-PROGRESS-20260910-002 — observation algorithm problem statement

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期目标确认：NO1--NO4 内部观测和 NO5 外部观测必须使用同一条物理测量定义——
  同一 `source/reference` 有向路径、同一校准 delay/bias generation、同一 sample
  sequence 和同一共同绝对生效时间；输出分别报告 raw phase、transport-corrected
  residual、固定 bias、真实 jitter、频率斜率、命令应用和观测覆盖率。FOLLOWER 只
  旁路 PI/DCO，不得减少自身观测点或用主机命令应用记录替代本地相位残差。
- 当前 C 端已确认的算法缺口：TDMA observation trailer 只编码 frozen-cycle phase；
  接收端将 `reference_tx_timestamp_ns` 置零并只保留本地 RX timestamp；没有随样本
  传递共同绝对生效时间、delay generation 或 bias generation。`vdc_ring_observer`
  又从本地 RX timestamp 重建 window start，这个值不能作为跨板 absolute-time anchor。
  因此目前 NO1--NO4 与 NO5 的数值不能证明处于同一时间窗，散点、微秒级偏差和
  丢窗既可能是算法语义错误，也可能是观测缺口，不能直接解释为锁相或失锁。
- 阶段 B 首个代码切片已落地：TDMA adapter 根据已校验的
  `correlated_sequence * cycle_period + reference_tx_phase` 生成
  `common_effective_time_ns`，并以 `TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME`
  明确标记；VDC observer 的窗口、start/observed/done/apply 时间全部从该 logical
  TDMA 锚派生，不再从本地 RX timestamp 重建。local RX timestamp 仍仅作硬件接收事实
  和 provenance，短帧布局及 Calibration training 未改变。
- 已验证：VDC、TDMA adapter、TDMA ring runtime、TDMA service scheduler 和 RefMem
  realtime TDMA host C tests 通过；snapshot 可读回 common time。仍未完成 delay/bias
  generation、NO5 同窗关联和物理绝对时间闭环，因此不能据此宣称跨板锁相。
- 本次 P3 运行完成 quick diagnostic flow，证据目录为
  `out/HardwareAcceptance/20260910/p3-060954/`，使用异步 OTA build
  `20260909221000`；receipt 的 `strict_gates_passed=false`。失败事实为 coarse CLK
  topology readback mismatch 和 NO5 `insufficient_stable_circular_span_windows`，
  该结果不构成 common-time 算法或正式锁相通过证据。
- 启动条件：先在 TDMA/RefMem/VDC 之间冻结 source/reference、sequence、CRC、共同
  时间锚、delay/bias generation 的 owner 和拒绝规则；在锚点缺失时只允许 diagnostic
  raw evidence，禁止 corrected jitter、formal lock 或从机实时应用。契约冻结后再修改
  C 端字段/adapter/observer，并同步更新 host/C 单测和 P3 证据。
- 下一 gate：完成 `VDC-OBS-ALG-001` 阶段 B 的 delay/bias generation、物理方向和
  owner 审计；未完成前不把 NO5 外部曲线与内部 DPLL 曲线做跨板锁相结论。

### VDC-PROGRESS-20260910-003 — generation admission and capture schema v4

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：正式 path table 下，Core1 evidence admission 现在要求非零且匹配 active
  `calibration_generation`/`bias_generation`；缺失或不匹配分别以
  `VDC_DOMAIN_GATE_DELAY_GENERATION`、`VDC_DOMAIN_GATE_BIAS_GENERATION` 拒绝，不能
  通过 debug continuation 进入 PI/DCO、corrected jitter 或 formal lock。临时训练表
  仍保持 diagnostic-only 语义。DPLL capture record 升为 schema 4，保留 generation
  provenance；decoder 继续兼容 schema 1/2/3。
- 软件验证：`run_vdc_domain_tests.ps1`、相关 Python 观测回归（88 passed）和
  `cmake --build --preset pico2-release --parallel 4` 通过；新增覆盖正式 generation
  缺失/错误的 C gate 和 schema 4 decoder generation 保留测试。
- P3 证据：当前源码指纹下 quick diagnostic 完成，证据目录为
  `out/HardwareAcceptance/20260910/p3-064132/`。receipt 的
  `strict_gates_passed=false`；TRN-01/03、TDMA startup barrier、NO5 RX bad counter
  和时间预算仍失败。该结果只证明验收流程到达内部/NO5 观测阶段，不构成正式锁相或
  `FORMAL_LOCKED` 证据。
- 边界：generation 目前已进入 C 端正式 admission 和 capture provenance，但 NO5
  waveform quality flags、内部/外部同窗关联、segment continuity 和质量报告仍未闭环。
- 下一 gate：补齐 raw-only/corrected-eligible 的 waveform flags 与同窗关联，再执行
  `1M3F`、`2M2F`、`3M1F`、主站切换及丢样/坏帧/背压故障注入。

### VDC-PROGRESS-20260910-004 — waveform quality separation and diagnostic SVG layers

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：NO5 waveform schema 4 的每条记录显式保留 `sample_seq` 和
  `quality_flags`。质量位区分 timestamp eligibility、sequence continuity、source
  drop、matched-window validity、gap、ambiguous edge、incomplete window、raw-only
  和 corrected eligibility。decoder 对旧 schema 继续兼容，但只把可证明的字段推导为
  弱质量事实；sequence/capture gap 会清除 corrected eligibility。
- 分析边界：raw tracking 继续保留用于诊断；phase/jitter/convergence、CSV 和 summary
  的正式统计只使用 corrected-eligible 且窗口完整的样本。SVG 同时显示 raw-only 灰色点、
  incomplete window 标记和 corrected 曲线，并写出质量 flags，避免把观测缺口误读为
  节点 jitter 或锁相失败。
- 软件验证：`python -m py_compile tools/dpll_waveform_capture/dpll_waveform_capture.py`
  通过；DPLL waveform/observation decode/residual analyzer 回归为 `44 passed`。
  新增 schema 4 quality 保留、置信度兼容和 SVG 分层覆盖。
- 边界：尚未完成 C 端与 NO5 的同窗 sequence/capture-generation 关联，也没有新的
  当前源码 P3/HIL 证据；本 checkpoint 不证明任一节点 `LOCKED` 或 `FORMAL_LOCKED`。
- 下一 gate：完成阶段 B 的 C 端 source/reference、delay/bias generation 和共同时间
  锚审计，再实现阶段 C 的内部/外部同窗关联及坏帧、丢样、跨 segment 缺口故障注入。

### VDC-PROGRESS-20260910-005 — schema-v4 build and P3 preflight result

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 构建：`cmake --build --preset pico2-release --parallel 4` 通过。schema 4 记录扩容后
  首次链接超出 RP2350 RAM；将 `VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS`
  从代码原值调整为当前符号定义的 416，并重新链接通过。分段、drop 计数和 decoder
  连续性语义未改变。
- 软件验证：VDC/RefMem/TDMA host unit scripts 全量 `37/37` 通过，观测 Python 回归
  `44 passed`，文档门禁与文档回归 `18 passed`。
- P3：当前源码指纹下运行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`
  使用 build `20260909231608`，在 Latency Cal profile apply 前置阶段停止；板
  `2BD5090FE009FA2A` 回读 `active_level=0`，其余三板回读请求 level 7。原始证据位于
  `out/HardwareAcceptance/20260910/p3-071601/p0t-topology/`。未进入内部/NO5 观测，
  不构成算法或锁相结论。
- 下一 gate：先恢复四板 calibration profile 一致性，再执行当前源码 P3；算法侧继续
  完成 C 端物理方向、generation 和同窗关联，不以本次硬件前置失败修改观测结论。

### VDC-PROGRESS-20260910-006 — current-source P3 reaches observation gates

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- P3：当前源码指纹下完整运行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`，
  build `20260909232400`，证据目录 `out/HardwareAcceptance/20260910/p3-072352/`。
  流程完成但 `strict_gates_passed=false`，profile 为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`。
- 原始失败事实：coarse CLK topology readback mismatch、coded marker gate 未通过，
  NO5 为 `insufficient_stable_circular_span_windows`。NO5 只有 1 个观测样本，
  `timestamp_eligible=false`、`phase_round_count=0`；NO1--NO4 也只有单点快照，
  均为 provisional，不能据此判断锁相或 jitter。TDMA startup barrier 最终稳定，但早期
  仍记录 NO1 transport/header 差异、process reject 和 bitmap incomplete；这些事实保留
  在 `diagnostic.json`，不能被观测算法摘要覆盖。
- 算法边界：本轮 schema 4 质量层、raw/corrected SVG 分层已进入当前源码 build，
  但由于硬件 gate 没有产生可用同窗窗口，未验证 corrected jitter 或内部/NO5 关联。
- 下一 gate：修复 P3 的 topology/coded-marker 前置状态并收集多窗口、多点 NO1--NO4/NO5
  样本；随后执行阶段 C sequence、capture-generation、segment continuity 和坏帧/丢样
  故障注入，仍禁止将 provisional/diagnostic 结果升级为 `FORMAL_LOCKED`。

## 进度记录

### VDC-PROGRESS-20260908-006 — configurable DPLL role and oscillator discipline priority raised

- TODO task ID：`VDC-ROLE-001`、`VDC-ROLE-002`、`VDC-ROLE-003`、`VDC-ROLE-004`、`VDC-ROLE-005`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：将可配置 DPLL 控制角色提升为当前最高优先级。设计固定为每节点保留 PI
  能力，角色为 `MASTER` 时执行既有 local evidence 到 PI/DCO 路径，角色为
  `FOLLOWER` 时只接收显式 source slot 的已验证 peer command；从机 local evidence
  不得更新积分、rate、phase 或成为隐式 fallback。训练与 Calibration 只测量时延，
  不因角色改造改变。
- 实现计划：先完成 Domain control profile 与 role switch 清理，再完成按 source slot
  的 RefMem command retention 和 manager apply，随后接入 Flash/SCPI staging/store；
  再建立不改写 DDS phase owner 的本地晶振 trim、clock-model 连续性和 stale/fault
  freeze，最后执行主从组合、切换、陈旧/错误来源/丢命令/trim fault 的故障注入与 P3/HIL。
- 证据与边界：本 checkpoint 仅冻结任务优先级与验证边界，尚无本切片源码、构建或 HIL
  结果；不宣称角色模式已生效、DPLL 已收敛或 `FORMAL_LOCKED`。
- 下一 gate：`VDC-ROLE-001`。实现并运行 Domain host C 单测，证明 master local PI
  保持可用、follower 旁路 local PI 且 role switch 清理旧积分/连续锁定状态。

### VDC-PROGRESS-20260908-005 — four-slot live batch coalescing

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `6297816` 将 `SYNC_IO_LOGIC_ANALYZER_CORE0_BATCH_SLOTS` 扩展为
  四个，并让 Core0 drain 在同一 capture sequence 内合并多个 `READY` batch，直到
  调用方 capacity 用尽；不会读取 active producer ring，容量或 capture 不匹配的
  slot 会保留为 `READY`。实时 phase decoder 和 record/header/schema 未改变。
- 软件与构建：`run_sync_io_logic_analyzer_tests.ps1`、全量 Python 回归
  （`788 passed`）和 `cmake --build --preset pico2-release --parallel 4` 均通过；
  release package build id 为 `20260907155445`。
- P3 证据：快速五板诊断完成，证据目录为
  `out/HardwareAcceptance/20260908/vdc-live-batch-coalesce-p3-20260908/`；
  `check-staged`、pre-commit 和 staged 源码指纹通过，TDMA process-image 通过。
  本轮 NO1-NO4 内部 DPLL SD 采样与 SVG 已生成。
- 失败与边界：NO5 观测在 TDMA preflight 发现 NO1 的
  `ring_adapter_rx_bad_count` 增长后未写出完整 `summary.json`，因此本轮没有新的
  可比 NO5 dropped count；诊断证据保留在 `diagnostic.json` 和
  `dpll-no5-observation/progress.json`，不能宣称正式 DPLL lock 或 strict gate 通过。
- 下一 gate：继续 `VDC-OBS-001`，在稳定 TDMA preflight 后运行 NO5 长时间观测，比较
  4-slot 合并前后的 dropped/segment 连续性；若仍有背压，再评估 StorageAO 写入节流。

### VDC-PROGRESS-20260908-004 — segmented trace export compatibility

- TODO task ID：`VDC-OBS-001`、`VDC-OBS-003`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `b11a74b` 修复离线导出工具只识别旧版
  `analyzer_<session>.bin` 的问题，使其同时发现固件 schema 2 的
  `analyzer_<session>_<segment>.bin` 分段文件，并保留 `segment_from_name` 身份；
  旧命名继续兼容。这样长期 live-batch 的分段不会在目录扫描阶段被静默漏掉。
- 软件验证：`tests/python/test_analyzer_trace_export.py`、
  `test_analyzer_trace_decode.py`、`test_analyzer_trace_batch_index.py` 共 `21 passed`；
  `py_compile` 通过。
- 构建与 P3：当前源码 build `20260908041206` 的四板 OTA、P3、TRN-00/01/02
  和 TDMA process-image/FIFO 证据位于
  `out/HardwareAcceptance/20260908/vdc-export-segments-p3-20260908/`；
  `check-staged` 和 pre-commit P3 指纹门禁通过。receipt 仍为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，DPLL/NO5 因 `--tdma-only` 跳过，且
  `strict_gates_passed=false` 的耗时边界仍保留。
- 失败与边界：本切片只修复导出发现，不代表已经完成 StorageAO 长期背压、drop
  interval、恢复点或断电恢复；也不能把 TDMA-only receipt 提升为
  `FORMAL_LOCKED`。完整 DPLL/NO5 失败原始样本和 SVG 继续保留在
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/dpll-no5-observation/`。
- 下一 gate：`VDC-OBS-001`。在真实板端长期 live-batch 上验证分段目录分页、下载、
  decoder/index 连续性，并补 StorageAO 背压、掉电/重启恢复的原始证据；完成前不推进
  `VDC-OBS-002` 或正式 DPLL lock。

### VDC-PROGRESS-20260908-003 — schema-v2 analyzer decode and sequence-wrap evidence

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `5661a94` 补齐离线 analyzer decoder/index 对固件 schema 2
  header 的解析，保留 `segment_index`、`first_record_sequence` 和 `batch_sequence`，
  并以 uint32 模运算识别记录与跨 segment 的序列间隔。`0xFFFFFFFF -> 0` 的正常
  wrap 不再被误报为 drop；该工具仍只描述已持久化的本地 pad-visible 数据。
- 软件验证：analyzer decoder/index 回归 `8 passed`；
  `tools/tests/run_sync_io_logic_analyzer_tests.ps1` 通过；
  `tools/tests/run_host_unit_tests.ps1` 全量 `37/37` 通过；`py_compile` 通过。
- 构建与 P3：当前源码 build `20260908032311` 的四板 OTA、P3、TRN-00/01/02
  和 TDMA process-image/FIFO 证据位于
  `out/HardwareAcceptance/20260908/vdc-observe-wrap-p3-20260908/`；
  `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` 和
  pre-commit 的 P3 staged 指纹门禁通过。receipt 为
  `config/hardware_acceptance/p3_acceptance_receipt.json`，验收范围明确为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，DPLL/NO5 因 `--tdma-only` 跳过。
- 失败与边界：该 receipt 的 `strict_gates_passed` 仍为 `false`；唯一记录失败为
  验收耗时 `324.452s` 超过 `100.000s`，动作是 `DEBUG_BOUNDED_FORCE_CONTINUE`。
  该次运行不是完整 DPLL/NO5 验收，不能宣称 `FORMAL_LOCKED`。先前完整 DPLL/NO5
  失败样本、原始 segment 和 SVG 仍保留在
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/dpll-no5-observation/`。
- 下一 gate：`VDC-OBS-001` 保持 IN PROGRESS，继续做真实长期 live-batch，验证
  StorageAO 背压、drop interval、恢复点和断电恢复；在这些证据闭环前不推进
  `VDC-OBS-002`，也不重新宣称正式 DPLL lock。

### VDC-PROGRESS-20260908-002 — bounded live analyzer batch handoff

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `21cbe8e` 将 `EDGE_TIMESTAMP` 的 Core1 active ring 经固定大小、显式
  `READY` 状态的 batch slot 发布给 Core0；Core0 只读取已发布 slot。每批携带
  capture/batch/first-record sequence 和 drop 计数，STOP/complete 仅在最后批次排空后
  发布 shadow，重复 ARM 在 batch 或 shadow 未排空时拒绝。StorageAO header 同步记录
  capture、batch、segment 和 first-record identity，仍保持在 Core0 执行。
- 软件验证：`tools/tests/run_sync_io_logic_analyzer_tests.ps1`、
  `tests/python/test_sync_io_logic_analyzer_contract.py` 和
  `tools/tests/run_host_unit_tests.ps1` 均通过；后者包含全量 host unit suite。
- 构建与 P3：current-source build/P3 receipt 为 build `20260908020537`，证据目录为
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/`；
  `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` 和
  pre-commit 均通过，TDMA process-image/P3 原始结果在同目录。
- 失败与边界：NO5 外部观测仍有 SD segment drop，raw phase gate 未通过；
  `dpll-no5-observation/waveform/analysis/dpll_convergence.svg` 保留失败波形，不能作为
  收敛或 `FORMAL_LOCKED` 证据。该诊断失败未改变 TDMA 短帧验收结论。
- 下一 gate：`VDC-OBS-001` 保持 IN PROGRESS，收集该 live-batch 路径的长时间 wrap、
  StorageAO 背压、drop interval 和断电恢复证据；在 `SYNC-LA-003/005` 退出门禁闭合前，
  不得推进 `VDC-OBS-002`。

### VDC-PROGRESS-20260908-001 — debug admission continuation and five-board diagnostic P3

- TODO task ID：`VDC-EVID-001`、`VDC-VERIFY-001`、`VDC-OBS-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `d126fa2` 增加 debug-only admission continuation。recoverable evidence
  gate 的 raw code/slot/evidence sequence 由 VDC snapshot 保留，但该样本不进入 PI/DCO，
  不改变 accepted/rejected sample count；结构性 identity/schedule/window contract 错误仍
  严格拒绝。SCPI `DPLL:OVERRide` 使用 Core0 单槽 mailbox，TRN-03 只在同时指定
  `--diagnostic-continue` 和 `--dpll-provisional` 时启用，并记录 requested/applied
  generation。debug 状态下 RefMem 不发布 formal locked flag。
- 软件验证：VDC domain host C、RefMem VDC vector host C，以及 DPLL/NO5/SyncIO/TRN-03/P3
  Python 回归均通过；release build 和 staged hardware-acceptance fingerprint gate 均通过。
- 构建与 P3：五板 OTA 和默认 quick P0--P3/TRN-03 的 current-source diagnostic receipt
  位于 `out/HardwareAcceptance/20260908/vdc-debug-admission-p3-20260908/`。四板 TRN-03
  realtime/closed-loop、TDMA preflight 和 process-image soak 通过；每块 ring Node 的
  debug admission 都读回 `ACTIVE`，requested/applied generation 一致，TDMA receive 与
  transport reject 增量为零。NO1--NO4 internal DPLL SD capture 已保留。
- 失败与边界：该 receipt 的 strict gates 仍未闭合。NO5 raw waveform 有 SD segment drop，
  未产生完整 phase round；quick flow 也超过既有时间预算。两项原始原因保留在
  `diagnostic.json`、NO5 waveform segment 和 SVG 中，不能用于宣称收敛或
  `FORMAL_LOCKED`，也不应归因于 DPLL admission 或作为屏蔽 TDMA 节点的理由。
- 下一 gate：`VDC-OBS-001`。先完成 runtime producer-to-Core0 bounded batch 接口，再推进
  `VDC-OBS-002` 的分段流式 SD 写入，消除 NO5 长期观测的 storage backpressure/drop
  缺口；之后才重新评估 `VDC-EVID-001`、`VDC-SERVO-002` 和 `VDC-VERIFY-001`。

### VDC-PROGRESS-20260907-005 — T3 matrix identity and Windows progress publish recovery

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 结论：独立 T3 没有使用错误校准矩阵。验收内置和独立入口均调用
  `tools/calibration_ring_validate/trn03_closed_loop.py`；独立复现读取与验收相同的
  `trn03-matrix.json`，generation、topology/profile/schedule CRC、物理节点顺序和
  offset row 均一致。复位后独立 T3 的启动稳定门和四节点 process-image soak 通过，
  证明先前失败来自运行起点/首帧边界状态而非矩阵选择。
- 工具修复：`ProgressReporter` 不再固定复用 Windows 的 `progress.json.tmp`；每次发布
  使用唯一 pending 文件，目标被 IDE/扫描器短暂锁定时写入带序号 fallback 并继续实时
  gate。新增锁占用回归测试；`tests/python/test_trn03_closed_loop.py` 为 `108 passed`。
- 硬件证据：最终源码 build `20260907110305` 的 quick P3/五板 OTA 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r2-20260907/`；首次 T3 因
  `2BD5090FE009FA2A` ARM transient (`arm_result=8`, `-200 Execution error`) 失败，原始
  证据保留。复位后使用同一 package 的 `resume` 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r3-resume-20260907/`，T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，progress
  文件完整发布，NO1–NO4 SD 样本数为 `8/13/13/10`。
- 边界：NO5 外部观测仍因 sequence skew `14`、SD dropped count `424` 未通过；本轮
  TRN-01 SCK 仍无 replay-safe row。两项均保留为严格失败/诊断反馈，不能提升为
  `FORMAL_LOCKED`，也不屏蔽 TDMA 节点。
- 下一 gate：解决 SCK replay-safe 矩阵和 NO5 外部观测/SD 连续性，再推进
  `VDC-EVID-001`；保持 provisional DPLL 只作调试反馈。

### VDC-PROGRESS-20260907-004 — quick full-flow acceptance and T3 comparison

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 验收范围：按默认 `QUICK_DIAGNOSTIC` 执行 P0–P3、T0–T3 TDMA process-image/FIFO
  短帧闭环和 DPLL；未使用 `--full`。五板 OTA、P3、T3 均保留在
  `out/HardwareAcceptance/20260907/vdc-full-acceptance-r1-20260907/`。
- 结果：build `20260907095401`；OTA 五板通过；内置 T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，并保持四节点
  TDMA 运行。NO1–NO4 内部 SD 样本数为 `7/9/12/10`；曲线分析仍为诊断级
  `low_decimated`，不能提升为 `FORMAL_LOCKED`。NO5 外部观测因 ring sequence skew
  `54` 未通过，严格总验收保持失败事实；DPLL 反馈不隔离 TDMA 节点。
- T3 对照：验收编排器内置调用与独立入口均为
  `tools/calibration_ring_validate/trn03_closed_loop.py`、`process-image`、512 cycles、
  `--dpll-provisional`、clock evidence enabled、1 s/0.25 s soak。独立复现分别保留于
  `vdc-independent-t3-r1-20260907/`（persistent session）和
  `vdc-independent-t3-r3-short-open-20260907/`（`--short-open`）；两轮都在启动稳定门
  因 NO1 `rx_bad/transport_bad` 与 process reject 增长而超时。差异是验收前序 P0–P2/SMA
  与刚 OTA 的干净起点，以及串口时序环境，不是两套 T3 实现。
- 下一 gate：保持快速验收默认不开 T0–T3 capture；先处理 NO5/启动稳定性和 NO1–NO4
  收敛数据，再推进 `VDC-EVID-001`/`VDC-VERIFY-001`，不得用 provisional 或诊断结果
  宣称正式锁相。

### VDC-PROGRESS-20260907-003 — DPLL feedback without node quarantine

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：DPLL 失锁、phase residual 超限和 DPLL phase 的 WCET/deadline 计数保留为
  调试反馈；只要 TDMA UP/DOWN、process-image、FIFO 和基础收发连续，Core1 不再因
  DPLL phase 超限新增 `quarantined_mask` 或屏蔽节点。验收报告将 DPLL feedback 与
  TDMA 节点健康分开记录，调参器继续使用失锁/residual/frequency/reject 反馈小步
  调整并回退，等待连续样本逐渐收敛。
- 软件验证：相关 TDMA/P3 Python 回归通过；固件构建和五板 quick P3 证据分别保留
  在对应 `out/HardwareAcceptance/20260907/` 目录。当前硬件诊断仍可能因内部捕获
  无样本、NO5 SD dropped count 或波形稳定窗口不足而不构成 formal lock。
- 下一 gate：在不隔离 TDMA 节点的前提下重新收集 NO1–NO4 `FILTer?`/SD residual
  曲线，确认调参后的连续样本确实收敛，再评估 `VDC-SERVO-002`。

### VDC-PROGRESS-20260907-002 — debug Type-II PI tuning path

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：补齐 `loop_filter_integrator_ppb` 和 anti-windup；保留 FLL
  `last_frequency_error_ppb` 与 PI 积分状态的可观测分离。新增 debug SCPI
  `DPLL:TUNE`/`COEFficient`/`FILTer?`/`DEFAult`，通过 Core0 mailbox、Core1 service
  boundary 和 requested/applied generation 生效；新增 `tools/dpll_servo_tune/`
  对 NO1–NO4 逐步试探、评分、接受/回退并写入 JSON 原始响应。
- 调试语义：异常可解析参数不因产品范围被拒绝；实时路径对中间值做饱和保护，
  参数变更清空旧 acquisition/integrator history，不能自动提升 formal lock。
- 软件验证：VDC domain host C tests passed；DPLL/SCPI/残差相关 Python tests
  passed；极端 profile、signed SCPI tuple 和 anti-windup 负测已覆盖。
- 构建与 P3：本切片修改了固件、SCPI 和验收工具，必须在当前最终源码指纹下重新
  build/OTA/P3；在新 receipt 产生前不得提交或宣称硬件闭环通过。
- 下一 gate：完成当前源码指纹下的五板 quick P3，并使用调参器收集 NO1–NO4
  `FILTer?`/vector/residual 曲线；仍以 `low_decimated`/振荡事实为诊断结果。

### VDC-PROGRESS-20260907-001 — quick capture policy and internal DPLL SD evidence

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：quick 验收默认关闭 T0–T3 SD raw capture；只有异常路径保留原始波形。新增 NO1–NO4 内部 DPLL `TRACE` ARM/STOP/SAVE、SD 下载和 residual 分析；NO5 继续作为外部只读 waveform observer。验收输出默认按 `out/HardwareAcceptance/YYYYMMDD/<run>/` 分区，显式 `--full` 才使用 full bench 配置。
- 软件验证：150 项 calibration/P3/OTA/state-machine Python 回归通过；VDC domain 与 resource arbiter host 单测通过。
- 构建与 P3：当前源码指纹下 quick P3/五板 OTA 完成，证据目录为 `out/HardwareAcceptance/20260907/vdc-internal-dpll-r2-20260907/`；NO1–NO4 各自产生可读 SD capture 与 residual SVG，NO5 waveform 另存于 `dpll-no5-observation/`。
- 结果：内部捕获链路通过，但样本量与 decimation 仍不足以宣称 formal convergence；分析报告标记 `low_decimated`，DPLL gate 失败事实保留在 diagnostic receipt。
- 下一 gate：继续定位 NO1–NO4 residual oscillation，并在增加稳定样本/完整证据后推进 `VDC-SERVO-001`。

### VDC-PROGRESS-20260906-004 — 三件标准文件基础重建

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：重建 VDC Architecture/TODO/Task Progress canonical 正文；历史版本复制到 `docs/legacy/vdc/`。
- 架构结果：明确 STATE_MACHINE、TDMA Foundation、Calibration、VdcSyncAO、SyncDpllFB、VdcVector、RefMem 和 Trigger 的 owner 边界；分离资源生命周期状态机与 VDC 锁相状态机。
- TODO 结果：建立从 resident cycle、active calibration、formal evidence、FLL 粗锁、Type-II PI、promotion、snapshot、HOLDOVER 到 RUN/HIL 的唯一迁移顺序。
- 验证：本记录完成后执行 docs_check、doc_regression、文档 pytest 和 Git Bash pre-commit。
- 证据：历史原文快照位于 `docs/legacy/vdc/`；工具中间快照位于 `out/doc-archive/vdc-20260906/`。
- 下一 gate：`VDC-TDMA-001`。

### VDC-PROGRESS-20260906-003 — 状态机域对齐检查

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：对照 `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`、`HAOFV_STATE_MACHINE_TODO.md` 和任务进度，确认 VDC 只消费 `RESIDENT_INIT -> RUNNING` 后的 cycle/latch evidence。
- 结论：`STOPPED/STAGED/ARMED/RESIDENT_INIT`、persona 切换、resource fault 和 diagnostic capture 不能产生 formal DPLL evidence；`RUNNING` 内的 `CYCLE_BOUNDARY -> LOCAL_UNLOAD -> LOCAL_LOAD -> FORWARD` 才是正式 TDMA observation 的来源。
- 阻塞：状态机任务进度中的 NO5 DPLL phase/SD writer 阻塞仍属于上游验收事实；不得用 TDMA short-frame 通过替代 VDC formal lock。
- 下一 gate：完成当前源码指纹下的 TDMA resident/hardware-latch evidence，再推进 `VDC-CAL-001` 和 `VDC-EVID-001`。

### VDC-PROGRESS-20260906-002 — TDMA 确定性同步方法与锁相模型

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 结论：VDC 采用固定 process-image/trailer、同圈 T1/T2/T3/T4、Calibration path matrix、FLL-assisted acquisition、Type-II PI tracking 和 coarse/formal promotion。
- 参考：LinuxPTP、Chrony、NTPv4/RFC 5905、EtherCAT Distributed Clocks、IEEE 1588 hardware timestamp、White Rabbit 和 TSN/gPTP 方法边界已写入 Architecture。
- 下一 gate：先闭合 TDMA/Calibration/evidence，禁止以 `LOCKED` 或 replay passed 冒充 `FORMAL_LOCKED`。

## 验证与证据规则

每个 checkpoint 必须至少记录：

- 对应 TODO Task ID 和 owner；
- 当前源码/build/config identity；
- host/build/test 命令及结果；
- OTA/HIL/NO5/SD 原始证据路径；
- 失败原因、后继状态、回退点和下一 gate。

诊断 replay、host 单测、TDMA short-frame、NO5 外环观测和正式 VDC lock 是不同证据等级，
不得相互替代。formal promotion 失败时保留失败证据，不修改为成功状态。

### VDC-PROGRESS-20260915-040 — STOP 后几何冻结与新 ARM 显式选择

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。
- TDMA owner 保存训练几何的拓扑、定向端点、PIO/DMA、引脚、相位、物理长度及 map 代际；完整 adapter STOP（含 RX station ACK）后才发布 `FROZEN`。部分取消、ARM 早退、persona/clock/资源变化和代际耗尽会退休描述符。
- 新 ARM 通过受保护 runtime config 选择精确 generation，并绑定新 config、ARM epoch、observation epoch 和 map generation；同一代际重复使用被拒绝。训练完成时记录 observation epoch，避免把 ARM 前空闲等待误当作训练来源。
- 软件 84 项回归、双槽 release 构建和 Flash 链接门禁通过；BSS 增加 792 B，FreeRTOS 堆和两核栈边界未变（资源快照，非容量契约）。当前源码四板 quick P3 严格通过，板端记录与 SD 下载逐字节一致；NO2–NO4 的冻结→选择→退休→陈旧拒绝 HIL 通过，NO1 保持 origin。证据位于 `out/HardwareAcceptance/20260915/frozen-geometry-r1/`。
- 仍未证明 CS 相对首帧坐标、完整自主身份关联、VDC 时间输入、命令应用或示波器正式锁相；下一步继续 TIME-002 的自主首次发车与 observer 就绪边界。

### VDC-PROGRESS-20260915-041 — 新 ARM 初始 observation epoch 的候选准入

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS；日期：2026-09-15。
- 代码提交：`d0a1efe`，本修复独立验收完成，父任务继续推进。
- 真实 `tdma_rx_dma_counter_reset()` 在每次 ARM 将 observation epoch 初始化为零；候选关联曾将这个合法初值无条件判为陈旧。本切片仅移除零值的额外拒绝，仍先验证非零 ARM/capture 身份、当前 ARM 有效性和 capture ID，再比较当前 observation epoch。observer/history/pin、复制范围及全部诊断资格标记保持原语义。
- 新增真实 counter reset/observe 回归：合法初值可以关联；计数不动但超过观测期限时旧候选被拒绝；STOP 后新 ARM 同样从零开始也不能复活旧 token。红测编译成功并在新 pin 断言失败，修复后相关软件回归通过，独立源码复核无阻断项。双槽 release 构建、Flash 链接检查和当前源码四板 quick P3 严格通过；全部 STOP 后的 SD 读回与 SRAM 原件逐字节一致，链接 RAM、堆和两核栈边界未变化。
- 本轮证据快照（非容量或时序契约）：软件回归 60 项通过，候选 harness 含 8 组、68 次查询；4 份原生记录及 37 项 hash 复核通过。源码指纹 `97899839f4ca72ec5f249aa3a5e80438c7d3cfd5145fb2253cf99cd5367f2560`，增量构建标识 `20260915022619`；构建标识沿用缓存，实际源码与 package hash 由本轮 receipt 核验。原件位于 `out/HardwareAcceptance/20260915/initial-observation-epoch/`，汇总为 `audit.json`。零初值的正反行为由真实生产函数 host harness 验证，普通 P3 只覆盖集成回归。
- 下一 gate：把 selected geometry 接入独立的 observer 预启动绑定与 START 前有界 DMA 观察。普通 origin 限发单帧先验证首条原始记录，随后再独立拆自主 origin 的非发射准备和显式首发；不通过单帧 sequence 相等授予物理身份，不放宽连续序列、配置、到期或取消检查。
- 范围：本修复不实现 observer prelaunch、CS 相对首帧坐标、自主正式时间输入、主从命令应用或 DPLL 锁相。只读后继设计与源码复核分别保存在该目录的 `next-slice-review.json` 和 `source-review.json`。

### VDC-PROGRESS-20260915-042 — 所选几何观察器预启动与普通首帧专项

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次只关闭所选几何预启动和普通 origin 限发单帧切片。
- TDMA owner 在 RX ARM、最终 capture WAIT、几何绑定及 SM 使能后，ARM 返回前同步尝试一次 observer 启动；失败走公共 STOP，不允许晚启。START 前的独立物理服务有界维护真实 DMA counter；冻结 A/b 保持独立诊断假设，不恢复或伪造 live alignment。配置、ARM/observation/map、时钟、训练不一致或观察器故障会退休诊断，不因局部计时失败隔离仍健康的数据面。GEOMetry 诊断读回追加 observer 生命周期；实际字段以 `scpi_cmd_system_tdma_ring_geometry_q()` 为准。
- 主控复核发现正常 disarm 先取消几何再停止 observer 会误记几何失效；上板前已改为先退休 observer，再冻结/退休几何并撤销 RX ARM。测试执行真实 ARM 末段和 disarm 前缀，覆盖正常 STOP 及 STOP 前已有故障，保留同代首个拒绝原因；新训练产生的新 FROZEN 代际清理其 observer 尾字段，不嫁接旧绑定。
- 验证快照（非容量或时序契约）：最终相关软件回归 77 项通过；双槽 release 构建、Flash 链接检查和当前源码四板 quick P3 严格通过。链接静态 RAM 增加 180 B，堆及两核栈边界未变。独立源码复核无阻断；主控审计核对 8 份 P3/专项原生记录与对应 SD 下载逐字节一致，以及 69 项文件 hash。当前源码指纹 `048e2507c0a74792e61e3510064aed5f84abab2e30dc6b095d9b82b53cb56977`；增量 build 标识仍为 `20260915022619`，版本由本轮源码、package 与 OTA receipt 共同绑定。
- 普通限发专项 `hil-r3`：三从所选 observer 在 START 前均为 ACTIVE，跨过空闲等待后 ARM/observer/observation 绑定不变，实时 alignment 和 RX 完成计数仍为零。板端 baseline 尚无已发布事件；首发后每从板仅发布一条 `sequence=1, ordinal=0` 的成对事件，epoch 不变。主板重复 START 未增加限发配额；STOP 后 CUT 仅含正常 STOP 退休原因，未决身份/在途/积压标记保持。SCPI 查询计数来自实际调用审计，采集区间为零；全部 STOP ACK 后顺序保存 SD。
- 失败原件：`hil-r1/r2` 在 ARM/START 前的 source seed 发布被拒绝；后继读回保留两次发布拒绝计数，四板 FIFO 均有一个 ready 槽和一个 active 槽。专项补齐既有 STOP 后 FIFO RESET，验证队列清空、冻结几何完全不变后再发布；另用真实主机响应解析器复现并修正专项脚本未识别 `FLIGHT:TX` 复合应答的问题。诊断时误用未定义的 `FLIGHT:STATus?` 及错误队列原件也保留。没有通过增加超时、复用已消费几何或放宽首帧判据追认失败；受 P3 指纹约束的源码未因脚本修正再次变更。
- 证据目录：`out/HardwareAcceptance/20260915/observer-prelaunch-r1/`，入口为 `audit.json`、`software-summary.json`、`source-review.json`、`p3/acceptance.json`、`hil-r3/hil.json` 及两组 SD 保存报告。最终四板 STOP、配置 ACK，主板限发恢复为关闭，各板选择几何清零；未操作 NO5、未修改 OTA。
- 下一 gate：独立实现自主 origin 非发射 READY 与显式首发，再证明首物理边界、唯一 packet/event 坐标及自主完整窗口。当前单帧序列相等不授予物理身份、正式时间戳或 DPLL qualification；所选路径的实际 WCET、TIME-003/004、主从命令、共同时间应用及示波器锁相均未关闭。

### VDC-PROGRESS-20260915-043 — 自主非发射 READY、显式放行与 DMA reload 校验

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭可控自主首发及对应取消/兼容切片，不关闭物理身份、正式时间输入或锁相。
- TDMA owner 在 INSTALL 中配置不触发的 loader，保持 SM、DMA 和驱动未启动；READY 按静态相位复验配置、时钟、资源、FIFO、PC、plan 和 loader。Core0 只发布绑定 trial/config 的显式 release 请求，Core1 消耗请求并在最终授权/到期复验后执行唯一触发。重复、旧代、耗尽及 STOP 取消仍拒绝；原有 TRIAL 在 READY 后通过同一 release 路径自动启动，不改变后续硬件自主循环的发车 owner。
- STOP 终态读取使用独立发布 token，避免 STOP ACK 早于下一相位取消时读到未完成结果。已发布终态保持不可变，迟到请求或清理不覆盖它；首次物理拒绝的 code/observed/expected 在公共 STOP 清零工作区前复制。SCPI schema 以 `scpi_calibration_origin_release_q()` 为准，release 尝试原因以 `tdma_origin_release_attempt_reason_t` 为准，physical_reject 的高位物理原因以 `tdma_origin_reject_reason_t` 为准；历史 schema 不补造不存在的物理诊断字段。FIFO 拒绝支路额外读取一次非破坏性 FSTAT 用于归因。
- 实板 INSTALL 拒绝已定位并修复：`positive-r2` 的首次拒绝为 LOADER 的计数检查，live 剩余数为零而期望待启动描述符长度。RP2350 的 TRANS_COUNT 写入只设置 RELOAD，触发后才装入 live counter；READY 改为读取完整 `DBG_TCR`，连同模式位一起精确比较。旧 host facade 把写入直接反映到 live 读数，曾掩盖该问题；修正模型后先复现与实板相同的拒绝，再验证正常 reload、残余 live、错误长度及自触发/无限模式。此前 `positive-r1` 没有细分原因，仅保留其 INSTALL 失败事实，不反向追认原因。
- 验证快照（非容量、时序或精度契约）：最终相关 host 241 项通过；双槽 release 构建与 Flash 链接检查通过；静态 RAM 相对前一已提交基线增加 200 B，堆及两核栈边界未变。源码指纹 `8ee638d0acd2e5d0aaa5a0b4bd40bba622e374362a4d841dcb8d7d309bb29fe6`，增量 build 标识仍为 `20260915022619`，由源码、package、OTA 与 receipt hash 共同识别。当前四板 quick P3 的 passed/strict_gates_passed 为 true；diagnostic_continue 为 true、failures 为空，不能据此声明自主时序或 DPLL 门禁通过。
- 专项原件：`positive-r3` 的主板 READY 连续原生样本覆盖静止区间，owner 执行 1237 次 READY 复验，保持约 1.854 s 后接受一次显式放行；重复请求被拒绝，四板后段接收计数均增长。`expiry-r1` 验证未放行时到期取消，`cancel-r2` 验证 STOP 后旧请求不能复活，`legacy-r1` 验证原有自动启动恢复运输。各项均保留完整计划记录，运行采集期间无 SCPI 查询；全部 STOP/ACK 后逐板保存 SD 并逐字节核对。专项使用几何选择关闭的路径，不授予首自主记录、唯一帧身份或物理精度。
- 流程失败仍保留：`cancel-r1` 的 owner 取消和旧请求拒绝正确，但提前 SCPI STOP 也取消了未完成的原生采集，完整窗口验收失败。后继在许可证仍有效时先完成 READY 采集，再 STOP/ACK 并重试旧 release；该证据不包含 STOP 边沿前后的连续原生样本。两版专项工具继承的“STOP 不取消 recorder”旧注释已被失败原件否定，实际语义以该失败与后继控制顺序为准；未改动已绑定原件的脚本。离线检查后继补齐实际脚本 hash，早期来源标签不完整的报告仍保留。
- 证据目录：`out/HardwareAcceptance/20260915/origin-ready-release-r1/`。入口为 `audit-r3.json`、`software-summary-r3.json`、`physical-reload-summary-r1.json`、`source-review-r3.json`、`p3-r3/acceptance.json`、四项专项及各自 SD 保存报告；`install-count-diagnosis-r2.json` 保存寄存器根因。主控从二进制重新解码并复算各轮实际脚本判据，核对 P3/专项的 20 份原件与 SD、429 项 hash，并保留三个失败轮次及其 SD 原件。四板最终 STOP、原配置恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：有界保留首自主原始记录并绑定当前记录代际、版本及 seed 后继序列，随后证明唯一 packet/event 坐标和完整自主窗口。正式时间输入、真实 DPLL 更新预算、主从命令与共同时间应用、示波器锁相及恢复门禁继续分别验收；长期目标保持 active。

### VDC-PROGRESS-20260915-044 — 首自主原始记录的一次性留存与原生采集

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭首自主原始记录留存切片，不关闭同一物理帧身份、完整自主连续性、正式时间输入或 DPLL 锁相。
- TDMA owner 的初次记录入口先将完整 `tdma_origin_record_t` 复制到专用首档，再独立写入 `TDMA_ORIGIN_FIRST_RECORD_VERSION`，随后进入原 ring0。后续循环不再进入首档入口，原槽位与发布版本的映射保持；缺返回、运输未获准或缺边沿的首条均保留，不等待后续成功条替代。专用存储放在 physical owner 末尾，避免扩大共用 workspace 的对齐占用。
- Core1 在已退休的 SEED 边界清首档并发布本地代际和独立的 seed 后继序列；新 ARM、persona 或生命周期失效撤销读取许可。`tdma_pio_spi_phys_origin_get_first_record()` 在一次有界复制前后复验 guard、许可代际和 commit，检查原始格式及首尾序列；序列按无符号回绕处理。提交前中断不可读；完整首档在 STOP 后仍可读取，包括 STOP 尚未成功但资源仍由原 owner 保有的情形，不能因此释放仍在用的资源或授予正式输入资格。
- Core0 Storage task 经 runtime owner 读取首档，按 `DIAGNOSTICS_TDMA_RECORD_SCHEMA` 新增可选 `origin_first` 组。读取复验失败后清除本次全部字段及 available，防止 delta 继承旧代内容；普通模式、三从、READY 未释放和记录关闭时不要求该组可用，原基本快照有效掩码保持。历史版本的字段顺序、类型及长度保持；当前原生格式与 DMA raw-time 格式分别版本化，均不赋予物理时间戳或 DPLL 权限。
- 软件与资源快照（非容量或时序契约）：最终联合 host 306 项通过，专项工具离线检查 44 项通过；实际构造图覆盖编译容量矩阵、有效 mask/local slot、guard/abort/记录开关，以及首条未获运输校验、逐字中断、提交后原 ring0 发布前 STOP、跨代复制、版本/序列回绕和多轮覆盖。当前六节点矩阵最大 317/320 个描述符、134/140 个 literal，单步最大 22/24；双槽 release 与 Flash 链接检查通过。相对前一已提交基线静态 RAM 增加 200 B，其中 physical owner 增加 104 B、记录 delta 状态增加 96 B，workspace、堆和两核栈边界未变。目标编译的局部栈报告为 app snapshot 3096 B、record_sample 3992 B、recorder service 128 B、首档 getter 56 B；这些局部值不是完整任务调用树峰值或 WCET 证明。其他编译容量的 host/ABI 结果不代替相应目标配置的硬件验收。
- 当前源码指纹为 `b8c30841fd4c3a58bb8e4cc40bfc0b624a23822e704f305ad18bfdafd106aaf9`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。`p3-r1` 的 passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空；四板短帧 closed_loop_passed/realtime_gate_passed 为 true。P3 跳过 DPLL 观测，不证明自主时序、全部 TDMA WCET 或锁相。
- 实板正向 `positive-a-r1`：首条 sequence 为 242，匹配 seed 241 的后继，记录 epoch 为 1；原生后续 10 个样本首档全组不变。STOP 尾档 sequence 为 3914、published_version 为 7346，前进 3672 圈，精确符合原槽位发布映射。首条 flags 为零而尾档获得运输校验标记，实证未以较晚成功条替换首条；不能仅据 flags 推断具体 CRC 失败原因。
- 新 ARM 后的 `cancel-r1`：记录 epoch 为 2，完整计划窗口内首档始终不可用且字段全零；先完成 READY 采集，再在授权期限内 STOP，旧 release 被拒绝。该窗口不包括 STOP 边沿前后的连续原生采样。再次 ARM 的 `positive-b-r1`：记录 epoch 为 3，首条 sequence 为 243，后续 10 个原生样本不变；尾档 sequence 为 3902、published_version 为 7320，前进 3659 圈，首条仍未获运输校验、尾档标记有效。两轮三从的可选首档均不可用。
- 验收工具独立复核补齐了跨 trial 代际比较及尾档版本/序列精确对账；正向释放边界不能把不同 guarded read 的 FSM 和首档当成同瞬时快照，因此仅在完整采样时间包络明确早于 release 请求时要求首档为空，包络仍保留显式工程时钟速率假设。未释放轮全窗空首档是独立负证据。首版 app host 夹具曾因重复定义已有 TDMA 类型而编译失败，修正夹具后通过；原失败 XML 保留。未放宽物理身份、CRC、序列连续性或取消规则。
- 证据目录：`out/HardwareAcceptance/20260915/origin-first-record-r1/`，入口为 `audit-main-r1.json`、`software-summary-main-r1.json`、`physical-summary-r2.json`、`source-review-r1.json`、`p3-r1/acceptance.json`、三轮专项及对应 SD 保存报告。主控从原生二进制重新解码并执行实际 HIL validate，核对 427 项 hash；P3 与三轮专项共 16 份记录均在四板 STOP/ACK 后顺序保存 SD 并逐字节一致。各轮采集中无 SCPI 查询，四板最终 STOP、SD 已保存、原配置恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：将受控自主首发与三从 observer/capture 起点关联，证明唯一 packet/event 坐标并保留完整自主窗口。本轮几何选择关闭且经历普通 bootstrap，首档不可覆盖不等于三从具有自主零起点。随后接入正式时间输入、验证真实 DPLL 更新预算，再推进命令、共同时间应用和示波器锁相；长期目标保持 active。

### VDC-PROGRESS-20260915-045 — START 当前代准入、取消与自主重臂回传阻塞

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次收敛 START 控制修复，普通四板 P3 和未放行取消专项通过；正向自主重臂恢复仍失败，首 RX 窗口留存尚未实现。
- 代码提交：`bb44c11`；独立受限硬件复核为 `hardware-review-r1.json`，回传候选原因的只读证据为 `rx-return-diagnosis-r1.json`。后者不是已验证修复或根因认证。
- `tdma_ring_runtime_set_data_enabled()` 原先把 `adapter_started` 当作 ARM 成功依据，但失败 ARM 的待清理状态也保留该字段。本切片一次有界读取配置和结果 guard，要求当前配置已由 adapter/applied 发布、无 pending STOP、TRAIN 请求已接受，并在发布前复验；奇数或变化直接拒绝。Core0 的 `tdma_service_ring_start()` 复用既有无等待控制锁，串行化 START 与 STOP/ARM/configure；DATA 发布后复验配置退休，覆盖 Core1 自主 lifetime 的取消写者。普通结果发布不撤销已接受请求，Core1 不增加锁或等待。
- 语义边界：成功只表示观察到当前代 ARM 完成发布且未观察到待清理，并提交 DATA 请求。result guard 未包围所有 adapter 字段的早期写入，不能宣称同瞬时完整硬件快照或持续健康；TRAIN 后可能仍需下一 Core1 相位恢复 persona。发布后取消可以返回拒绝并清 DATA，但不证明请求从未短暂可见。observer ACTIVE、物理首帧和同圈身份仍需各自原件。
- 软件与资源快照（非容量或时序契约）：旧生产代码的真实 C 红测有四个断言失败；修复后相关回归 100 项和完整生产 service/runtime 原子交错 33 项通过，runtime/service scheduler 两套 C suite 通过。覆盖部分 ARM、清理未完、奇数 guard、旧代发布、读取中变化、Core0 控制互斥及 Core1 在 DATA store 前后取消。双槽 release/Flash 链接通过，静态 RAM 增量为零，堆及两核栈边界不变。源码指纹 `008e770147d04660c49dd56e56c67b9865d400a0d40feb449e7d2c4588c4f53c`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。
- 当前源码 `p3-r1` 的 passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空，DPLL 观测为 `SKIPPED_TDMA_ONLY`。它只关闭普通四板短帧集成回归，不关闭自主回传、TDMA 全部 WCET 或锁相。独立源码复核见 `source-script-review-r3.json`，P3 原件复核见 `p3-evidence-review-r1.json`。
- `positive-r1` 在 NO3 START 收到 helper 将超时包装成的 `OK(no payload; verified by state readback)`，没有实际进行所声称的状态查询；专项要求原始 OK，正确停止且未执行 release。STOP 后错误队列保留 Execution error，不能推断具体 runtime 拒绝分支。失败、部分采样和对应 SD 均保留，不把这项结果追认为成功。
- `positive-r2` 的 START 均明确返回 OK；三从选用新冻结代际重臂，首发前 baseline 和前段样本均显示新 ARM/observer 绑定、ACTIVE、零事件和零 DMA 完成。四板计划样本各 20 条、missed/reason 均为零；显式 release 后 NO1 accepted 全窗固定为 237，首档 sequence 241 与 seed 240 的后继一致，尾档已到 4959 但首尾 flags 均为零。NO2 的 overlay_prepare_count 增长至 1251，NO3/NO4 在计划采样窗口内仍为零，后两板 RX observation drop 接近接收次数减一。STOP 后冻结描述符已显示训练完成，因此不能宣称整个 ARM 永久未训练。重臂清 live alignment、一次 discovery hint 与后续扫描跳帧导致连续确认不足是当前候选原因，尚未通过独立实板修复确认；原 `hil.json` 保持 FAIL，`restart-analysis-r1.json` 仅补充受限诊断。
- `cancel-r1` 未放行时完整窗口内源保持 READY、首档不可用，三从保持新 observer ACTIVE 且事件/DMA 均为零；完成计划采样后全板 STOP/ACK，源终态为已取消，release_ticks 为零，旧 release 由冻结拒绝计数和原因确认不能复活。终态读回距工程最早到期约 4.29 s，计算显式假设时钟速率误差上界，不是校准共同时间，也不覆盖 STOP 边沿两侧的连续采样。取消专项通过。工具以实际 action 索引严格检查 STOP→ARM→START，时间允许相邻操作落在同一主机计时 tick；离线正反检查 32 项通过。旧工具与错误夹具原件保留。
- 证据目录：`out/HardwareAcceptance/20260915/ring-start-ack-r1/`。主控 `audit-main-r1.json` 从二进制重解码并复算当前判据，核对 60 项 hash、P3/两轮失败/取消共 16 份原生记录及逐字节相等的 SD 下载，明确 `autonomous_restart_recovery_passed=false`。运行采集期间没有 SCPI 查询，全部 STOP/ACK 后顺序保存 SD；最终 `final-board-state-r1.json` 确认四板 STOP、记录已保存、诊断模式和主板不限发配置恢复。未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：先独立解决重臂后自主有效回传停滞，每个功能切片继续 host→release→当前四板 P3→专项原件复核；随后保留首 RX 原始窗口、证明 packet/event 同物理帧关联，才开放 TIME-003/004。后继依次验证共同时间和实际更新调度预算、主从命令定时应用、NO1/CH1 触发的四路输出锁相、恢复与长稳；长期目标保持 active。

### VDC-PROGRESS-20260915-046 — 重臂后单帧发现刷新与四板自主回传恢复

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭重臂后 live alignment 训练停滞及自主有效回传恢复切片，正式时间输入、同物理帧身份、时序总门禁和 DPLL 锁相继续待验收。
- 真实异步捕获红测复现前轮候选：首次 Core0 私有发现仅含一帧，结果交付时 DMA 已前进多个帧长，后续持续跳帧；普通 RX 成功但相邻确认始终不足。TDMA owner 现在只在 process image 尚未训练且未锁定时，用有效 hint 预测最近一对已完成邻帧的位置，有界复制到原私有 job，再由 Core0 按既有运输解码规则确认相同位移、长度及精确 stride。hint 定位本身不授予训练或物理身份，不改冻结几何的独立诊断含义，不增加实时解析或动态存储。
- 原有效 RX hint 在刷新 REQUESTED/BUILDING 期间继续服务，同上下文的无效刷新结果不清除它；陈旧 request/observation/persona/配置仍拒绝。Core1 对每次复制分别进行 DMA 完成计数、epoch 和覆盖复验；刷新失败不推进普通 RX 游标，且本次不再发第二次 discovery，避免形成三次复制。达到训练要求或已锁定后不再刷新。STOP 后 worker 尚占用的 job 必须完成取消 ACK 才能复用；坏首帧、缺字和旧代记录不能因后继成功而升级。
- 软件及资源快照（非容量、时序或精度契约）：相关 RX/cursor 49 项、候选/预启动/DMA/adapter 集成 9 项通过；覆盖各位移、持续跳帧、单帧/不完整/损坏第二帧、取消三态、复制覆盖等值拒绝、代际变化及整数边界。刷新私有窗口上限 604 B，复用原 616 B 缓冲；刷新加普通复制上限 901 次 DMA word 读取，全函数既有坏 header 加初始发现路径保守上限仍为 913 次，每调用至多两次复制。它们是操作数上界，不是芯片 WCET。FLIGHT_MUTABLE 的 CRC 保护范围保持既有语义，不声称覆盖全部可变 payload。
- 首版目标构建因训练辅助代码内联进 SRAM 跨过 BSS 对齐边界，多占 4096 B，原 map 和源码已保留；将训练刷新及 READY 结果合并显式放入 Flash，稳态入口短路后重新构建，双槽 release 和 Flash 链接通过，静态 RAM 净增为零，堆和两核栈边界未变。刷新 helper 仅在未训练且未锁定分支调用，READY helper 仍按 job 结果就绪调用，不能豁免其静态调度预算。源码指纹 `161dd3f2aed96cf86d0ba510807b4593d3f2350984b05a2c6238a75a0d1f1dd6`，build 标识仍为 `20260915022619`，通过源码、package、OTA 和 receipt hash 共同识别。
- 扩展 host 曾在 `test_descriptor_completion_and_bounded_stop` 编译失败，直接重放保存 stderr 确认旧夹具缺少前序新增的 `tdma_geometry_trained()` 依赖。补齐只记录通知的 facade，验证已满足训练、首成功提交仅调用一次、失败与 STOP 不额外调用；原 DMA/STOP 断言保持，编译和运行命令、返回码及输出均留存。该夹具未包含本轮 RX 文件，原失败不归因于刷新算法，后继完整集成回归通过。
- `p3-r1` 四板 OTA 和初始化完成后，NO2 的 OPMode APPLY 超时，读回 active 为旧级别、reject_count 增加，last_result 为 BAD_ARGUMENT。四板 STOP/ACK 后只重试一次 STAGE/APPLY，原始应答及 active 读回确认成功；没有具体子分支证据，不追认为原 APPLY 成功或归因为 RX。保持同源码重新运行 `p3-r2`，passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空，普通四板短帧通过；DPLL 为 `SKIPPED_TDMA_ONLY`。
- 新目录内 `positive-r1` 在普通 bootstrap 的 START 收到 helper 对无有效响应的包装文本，严格原始 OK 判据拒绝，尚未 ARM recorder，records 为零；原失败保留。新 `positive-r2` 通过：三从 STOP→选新冻结几何→ARM 后，首发前 observer ACTIVE、事件和 DMA 完成均为零；四板各完成 20 条原生计划样本，missed/reason 为零，运行采集期间无 SCPI 查询。后段诊断窗口 NO1 accepted 从 409 增至 1509，三从分别为 156→1254、135→1233、114→1213；三从 prepare/published/selected 均增长。selected 计数表示既有 DMA selection 退休观察，不单独证明某片已在物理线上发出。这里的新 `positive-r2` 与前轮 `ring-start-ack-r1/positive-r2` 失败原件分别保存，不能混用。
- 首自主归档 sequence 为 240、flags 为零；后续首档保持不变，STOP 尾档 sequence 为 4956、flags 为一，记录的 sequence 前进 4716。恢复运输没有用较晚成功帧替换未获运输校验的首条。`cancel-r1` 在未 release 的完整窗口保持主板 READY、空首档及三从预启动静止，完成记录后在工程最早到期前 STOP，旧 release 拒绝；工程速率假设和取消终态读回保留，不能称为已校准共同时间。
- 时序仍有缺口：正向原生 baseline 到末样本的 TDMA overrun 增量为 NO1 零、三从 18/54/61，deadline miss 增量为零及 6/36/40。对应 profile reset 后整相位 peak 约 656/895/930/933 µs；NO1 峰值跨 READY→自主迁移，NO4 峰值含 STOP 清理，不能全部当稳定 RUN 或刷新函数耗时。NO2/NO3 峰值在已接收数据的处理相位，未含 RX copy；当前资料不能从这些峰值单独推导刷新时延。累计历史 max 不当作本窗 WCET，恢复回传不等于时序通过，不新增健康节点隔离。
- 证据目录：`out/HardwareAcceptance/20260915/rx-training-refresh-r1/`。入口为 `audit-main-r1.json`、`software-summary-main-r2.json`、`host-evidence-r1.json`、`design-source-script-review-r2.json`、`hardware-review-r2.json`、`p3-r2/acceptance.json`、`positive-r2/hil.json` 和 `cancel-r1/hil.json`。主控重解码、复算实际 HIL 判据并核对 51 项 hash；独立审核复核这些 hash、重解 12 份原生记录并重建 58 页 SD 原始应答。P3/正向/取消共 12 份原生记录在四板 STOP/ACK 后保存 SD，下载逐字节相等。代码提交 `50b46c1`。最终四板 STOP、记录已保存、诊断配置和主板不限发状态恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：继续首 RX 原始窗口一次性留存，再证明自主首发与三从 packet/event 的同物理帧关联和完整连续性，才开放 `VDC-TIME-003/004`。从板处理相位预算另行闭合；之后推进真实 DPLL 更新、命令运输与共同生效时间、示波器锁相及恢复长稳，长期目标保持 active。

### VDC-PROGRESS-20260915-047 — 首 RX 原始窗口、首事件及 STOP 后档案留存

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭所选几何预启动路径的首 RX 诊断档案切片，不关闭物理帧身份、完整自主连续性、正式时间输入、静态调度或 DPLL 锁相。
- TDMA owner 在 persona workspace 外有界保留 `tdma_rx_first_window_t`。原始坐标固定为首窗口，不按冻结偏移移动起点，不搜索较晚帧；DMA 完成计数达到窗口长度后只尝试一次，独立复验复制后的 epoch、配置、覆盖及硬件故障。复制失败仍保留已写字节，首失败不能重试替换。原始 FIFO 首事件独立保留，部分批次可累积，只有完整首 ordinal 且最终故障检查通过才置事件有效；原始字节有效与事件有效分别判定。
- 新 admitted config 撤销旧档，同 config 的失败重试不清档；persona unload 包括 load 失败回滚均退休原 lifetime。未绑定 observer 的早期 ARM 拒绝保留稀疏档案，不能继承旧 observer 的状态/故障。真实 STOP 先取消 pending，只有 DMA 和 worker/scanner 清理 ACK 后才标记 STOPPED；完成档案保留首原件。Core0 经 runtime owner 有界读取，失败清零，原生 `DIAGNOSTICS_TDMA_RECORD_SCHEMA` 增加 `rx_first` 组并兼容旧版本；跨核读取和原始档案本身均不授予 VDC evidence 资格。
- 软件和资源快照（非容量、时序或精度契约）：主控原生格式/应用/集成测试分别 60/2/8 项通过；物理路径修正前广测 108 项通过，独立审核修正稀疏 STOP 后的最终定向回归 7 项通过。覆盖编译容量、真实 persona unload/rollback、实际 STOP 入口及 ACK 尾部、DMA 复制复验、首 FIFO 部分事件和失败不重试；不能将修正前广测表述为最终源码全量重跑。双槽 release/Flash 链接通过；静态 RAM 相对前一已提交基线增加 1020 B，堆及两核栈边界不变。最终 ELF 复核的 native snapshot 最深已知链为 9572 B，加保守异常上下文为 9808 B，相对 Storage task 的 12288 B 配置余 2480 B；这只覆盖该调用链。四板 STOP 后 Storage 高水位均余 626 words，即 2504 B，不能替代任务全部路径的静态上界。
- 当前源码指纹 `9977b06303996436435d430b1ca4cae11cf48da490d3b48d45a4e406ca36b1b2`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。`p3-r1` 用时 190.548 s，passed/strict_gates_passed 为 true，diagnostic_failures 为空；普通四板短帧通过。DPLL 观测为 `SKIPPED_TDMA_ONLY`，该 P3 不覆盖自主全相位时序或锁相。
- `positive-r1` 完成四板各 20 个计划样本，missed/reason 为零。三从首发前 observer ACTIVE、零事件/零 DMA；首原始窗口各保留 173 B，复制前后完成坐标均为 173，首事件 ordinal 为零、word mask 完整，首因和硬件故障为零。冻结 A/b 分别为 3/0、1/5、0/2；按该几何离线提取，调用当前生产运输解码器均通过，sequence 为 242、identity 为 194528521，与首事件序列及主板首档字段一致。主板首档 flags 仍为零，未获回传运输校验；字段相等和诊断档案不可替换不能提升为同物理帧身份或正式时间戳。
- `positive-stop-r1` 验证完成首档在 STOP 后原始字段不变，仅退休标记增加。`cancel-r1` 的 NO3 START 无有效应答，底层返回 `OK(no payload; verified by state readback)` 占位文本，但该分支未实际查询；HIL 严格拒绝并保存四份取消记录，没有 release，也不追认为成功。同固件另开 `cancel-r2`，完整未 release 窗口及到期前 STOP 取消通过；`cancel-stop-r1` 保持零 raw/事件，原因明确为 STOP 未完成。pending 阶段的 `copy_before_ticks` 随初始 DMA 观察更新，不代表执行过复制；离线聚合最初将它误列为不可变字段而失败，修正为未尝试复制条件下单调前进，原失败保留。`rearm-r1` 取得非零新配置和实际 ARM ACK，原生 baseline 的配置与 ACK 一致，旧首档全零；这些 STOP/重臂探针是主动 CANCEL 封存的 baseline-only 证据，不是成功 RUN 窗口。
- 时序门禁仍 FAIL：正向原生 baseline 到末样本的 TDMA overrun 增量为 NO1 零、三从 59/112/103，deadline miss 增量为零及 42/69/73。同 reset 的完整相位 peak 约 642.320/950.476/1068.012/1002.064 µs；NO1/NO3 峰值含 STOP 清理，NO2/NO4 为数据处理相位。三从首复制及前后观察/复验括号分别约 34.468/20.400/19.152 µs，不能当作纯 memcpy 或完整相位耗时，也不能解释全部增时。当前 TDMA 预算仍取 `app_realtime_profile.c` 对应静态配置；首档功能通过不豁免其实际全相位超限，也不新增健康 TDMA 节点隔离。
- 证据目录：`out/HardwareAcceptance/20260915/rx-first-window-r2/`。入口为 `software-summary-main-r1.json`、`event-core-handoff.json`、`implementation-independent-review-r1.json`、`final-build-stack-independent-review-r1.json`、`final-hardware-doc-independent-review-r1.json`、`audit-main-r2.json`、`p3-r1/acceptance.json` 及各专项原件。主控从二进制重解码、复算 HIL/STOP/重臂判据，核对 147 项 hash；含失败轮次的 28 份原生记录均在四板 STOP/ACK 后保存 SD，下载逐字节一致。独立审核另行重解这些原件，并重建 91 页 SD 原始应答核对相等。代码提交 `08fb5c2`。最终 `final-board-state-r1.json` 确认四板 STOP、记录已保存、诊断配置及主板不限发状态；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：继续证明自主首发、三从首 packet/event 与实际物理边沿的唯一对应，再验收完整自主窗口、接入正式时间输入，依次开放 `VDC-TIME-003/004`。真实 DPLL 更新的共同时间与静态预算、主从命令定时应用、NO1/CH1 触发的四路输出锁相以及恢复长稳仍按依赖逐项推进；长期目标保持 active。

### VDC-PROGRESS-20260915-048 — 所选首帧入口的实际等待见证

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本切片关闭 capture 在 observer 就绪前提前采入部分位的具体歧义，不授予完整物理帧身份、正式时间戳或 DPLL 锁相。
- TDMA owner 在 observer 实际启用前后分别读取 capture SM 的 `EXECCTRL.EXEC_STALLED`，通过 `TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_BEFORE/AFTER` 留存；后半段使用 OR，保留前半段事实。selected prelaunch 同时核对两位及当前安装的 process follower 入口 PC，不满足则以 `TDMA_GEOMETRY_OBSERVER_CAPTURE_ENTRY` 进入原有拒绝/STOP 路径，不重注入、不等待后帧替换失败。普通未选择几何路径保持原启动行为。见证依赖已审计的末次 `WAIT 0 RXCS` 及其后无重装/重启的唯一 owner 生命周期；它不是任意等待指令或 PC 值的通用身份凭据。
- 旧生产源码在最终同一夹具下编译成功，运行以断言退出，复现已释放 WAIT、零 DMA 和空 FIFO 被接受；原件为 `old-production-final-fixture-red-r2.json` 及对应目录。此前旧模型提取器未适配几何绑定调用、首轮新增测试遗漏档案 admission 的失败分别保留；修正夹具后最终六组文件共 127 项通过，测试前后源码 hash 一致。覆盖启用前/期间释放、前后 PC 错误、前错后恢复、首次拒绝不重试、普通路径、STOP/重臂和首档生命周期；host 不代表电气边沿证据。
- 资源快照（非容量契约）：首 cut 保持 128 B 和 schema 1，未增加 PIO 指令或全局缓冲；双槽 Release 和 Flash 链接通过，静态 RAM 增量为零，堆及两核栈边界未变。源码指纹 `2e61755a453b740193a28061463f4e0bba3f1460616cec485918173fa6cfcbd9`，build 标识继续为 `20260915022619`，以源码、package、OTA 和 receipt hash 联合识别。独立审核同时核对最终 ELF 的两次 MMIO 读取、标记保留及拒绝分支。
- `p3-r1` 为诊断流程完成、strict_gates_passed 为 false：TRN-00 residence trial 0 的 NO4 `TOPOLOGY 4,3,0` 返回 `<timeout>` 和执行错误，尚未进入 ARM；没有证据将其归因于新增 selected 检查。原始失败与四份普通 TDMA 记录保留，STOP 后 SD 全等。同源码有界重跑 `p3-r2`，passed/strict_gates_passed 均为 true，diagnostic_failures 为空，四板短帧通过；DPLL 为 `SKIPPED_TDMA_ONLY`。新通过不追认首次失败。
- 正向 `positive-r1` 与未放行 `cancel-r1` 均完成四板各 20 个计划样本，期间无 SCPI 查询；全部 STOP 后顺序保存 SD。三从两次入口等待位均为一，PC 与原生记录的实际安装偏移均为 4，cut flags 为 895，ARM/observer/config/map 与首档一致。正向固定首窗口各 173 B，A/b 为 3/0、1/5、0/2；生产运输解码器通过，sequence 为 241、identity 为 451227504，原始 sequence FIFO word 与冻结几何提取的对应位段一致。主板首档 flags 仍为零，不能凭内容相同赋予首回传运输资格。取消窗口维持零 raw/事件，STOP 在最早工程到期前取消旧 release；工程时间界不作正式共同时间。
- 主控独立入口审计共 17 项离线检查通过：旧实板记录缺少新等待位明确拒绝，实际 cut 应答与动作原件全等，入口 PC 同时与原生安装偏移核对，保留未知/诊断标记。整体审计重新解码含失败 P3 的 16 份原生记录，重建 79 页 SD 应答并逐字节核对，复算 HIL 判据及当前源码/receipt 等 104 项 hash；证据为 `audit-main-r1.json`。最终四板 STOP、记录已保存、诊断配置恢复，NO1 不限发；未操作 NO5，未修改 OTA 实现或配置。
- 时序快照（非 WCET 契约）：正向完整相位 peak 为 NO1 715.648 µs、三从 918.316/932.768/990.988 µs；NO1 包含 STOP 清理，三从为数据处理相位。baseline 至末样本的 TDMA overrun 增量为 0/45/30/44，deadline miss 为 0/31/17/27。当前 `app_realtime_profile.c` 对应预算的全相位门禁仍 FAIL；该入口检查不改变预算，也不证明总体提速，不隔离仍健康的 TDMA 节点。
- 证据目录：`out/HardwareAcceptance/20260915/physical-frame-identity-r1/`，入口为 `event-core-handoff.json`、`software-summary-main-r1.json`、`implementation-independent-review-r1.json`、`hardware-independent-observations-r1.json`、`start-gate-self-test-r3.json`、`start-gate-positive-r1.json`、`start-gate-cancel-r1.json`、`audit-main-r1.json`、`p3-r2/acceptance.json` 及各原始目录。代码提交 `30b5335`。
- 下一 gate：按 `physical-identity-next-evidence.json` 优先闭合实际 origin 唯一发车/首描述符与首档、首 CS 完整时钟数、capture/sequence/control 同位采样及无丢样，再合成首窗口不跨 CS 拼接证明。实际分频的周期量化和跨板同步相位必须计入；已有固定周期模型不能直接推广。之后依次验收 TIME-003 完整自主窗口、TIME-004 正式输入、真实更新调度/角色、命令运输和共同时间定时应用、FLL/PI 与质量、示波器输出锁相及恢复长稳；长期目标保持 active。

### VDC-PROGRESS-20260915-049 — 首帧优化退出锁相前置，有效样本驱动推进

- TODO task ID：`VDC-TIME-002/003/004`、`VDC-SAMPLE-001`；旁路任务为 `TDMA-REFINE-001/002/003`；日期：2026-09-15。本记录只关闭执行方案收敛，不关闭正式时间输入、调度、命令运输或实际锁相。
- 用户明确：启动首帧可以不可用，稳定传输后开始 DPLL；飞行数据已可流转，错误帧丢弃只减少更新机会，不直接干扰锁定。因此首发/首档对应、精细采样与首窗口不跨 CS 的三项完整证明归 TDMA 后续优化，均不作为 DPLL 推进前置。新增 `VDC_STABLE_INPUT_PLAN.md`，调整 TODO 依赖，保留已有代码及全部首帧成功/失败原件。
- 待实现语义为：任意合格稳态样本可以建立当前代际时间锚；偶发坏帧、缺帧、重复和旧样本跳过本次更新，保持积分、有效时间锚和可信 DCO，不要求连续无错固定样本数。收到旧代际数据与当前基准实际换代分开；持续缺失按年龄与保持误差处理，不能把过去锁定状态永久当作当前精度保证。下一有效更新核对真实时间间隔，Ki 步长与完整 HOLDOVER 分开切片。
- 只读源端审计原件位于 `out/HardwareAcceptance/20260915/first-frame-coordinate-r1/`：`origin-n1-proof.json` 保留实际 builder/PIO/记录顺序的局部因果结果；旧 repository 测试夹具缺 retirement seam 的失败仍保留，仅 out 副本补 seam 后的运行通过，不称为仓库原测试已通过。`capture-stall-lifetime-review.json` 只证明所观察 capture 生命周期内无对应 stall，不证明精度或完整物理身份。`origin-stable-window-reuse.json` 中连续无错窗口及 loss 立即撤销的早期建议由 `dpll-valid-sample-semantics-addendum.json` 和用户最新指令覆盖，不扩展为新门禁。
- 四板已有数据流转可直接复用：上一正向原件的 baseline 至末样本有效 RX 均增长，三从候选查询均匹配；这些事实不等于正式时间输入已接通。源码 `tdma_pio_spi_phys_origin_rx()` 自主时间戳仍为零，原始时间记录只供诊断。此前 P3 明确为 `SKIPPED_TDMA_ONLY`，尚无本轮 DPLL 输入阶段计数或 trace 实测结论。
- 实际控制差距已定位到 `vdc_domain_reject_requires_reacquire()`：多数输入拒绝会走 `vdc_domain_reset_lock_acquisition()`，清积分与频率锚；现有无新输入/重复输入返回已可复用。FLL 使用有效锚间隔，Ki 仍消费静态 `servo.update_period_us`；保持年龄字段存在不证明超时 HOLDOVER 迁移已实现。这些是下一可归因功能切片，不能用放行无效时间戳替代修复。
- 本方案按项目 collaboration/doc-self-regression 流程验证，独立审查确认首帧不阻塞、无连续 K 门禁、旧样本与实际换代区分及 HAOFV owner 边界。纯文档不构建/刷机或重跑 P3；文档门禁结果存入 `out/doc-audit/20260915-dpll-stable-input-r1/`，本记录不追认旧硬件失败，不冻结新契约。
- 下一 gate：用现有四板板端 trace 和 observer 阶段计数测量自主有效输入；TDMA native 从启动留证，DPLL trace 在计划自主阶段触发以免 bootstrap 填满。全板 STOP ACK 后顺序保存及读回，用计数增量与实际覆盖判断输入卡点，不将 STOP 后的 INACTIVE 当作 RUN 拒绝。`VDC-SAMPLE-001` 单独修复坏样本跳过，每个实际功能变化立即完成 host、当前源码构建、四板 P3 和专项，再返回时间输入及示波器锁相主线；长期目标保持 active。

### VDC-PROGRESS-20260916-001 — 单样本跳过收敛与四板自主时间输入定位

- TODO task ID：`VDC-SAMPLE-001`、`VDC-TIME-002`；日期：2026-09-16。关闭本次样本拒绝的软件与集成切片，实板坏样本恢复待自主有效输入接通；不关闭 `VDC-TIME-003/004`、完整 HOLDOVER 或实际锁相。用户进一步明确 DPLL/VDC 时间同步是 TDMA 特等席负载，已在执行方案中落点，继续复用固定 process-image/trailer 和优先预算。
- `vdc_domain_reject_requires_reacquire()` 将单份样本的来源、CRC、时间有效性、窗口和代际等拒绝与本地 disabled/非法参数/无效 schedule 分开；跳过时不清积分、DCO、有效时间锚和有效样本累计，不按 bad-count 直接重锁。拒绝计数继续增加，质量年龄不刷新，正式 gate 仍拒绝坏输入。紧凑输入将本地 schedule 无效单独归为 BAD_SCHEDULE，避免被 BAD_FRAME 的跳过策略混同。既有 debug continue 仍不执行被拒绝样本的 servo 校正。
- 没有新增时间戳、wire 字段、PIO、缓存、独立 Domain 重复过滤或 Ki 步长/HOLDOVER 功能。FOLLOWER 的本地原始观察记录保持既有行为；单独 publish clock/path/dictionary 的实际换代清理尚未闭合。set_ready(false) 保持原 OFF 语义，不宣称它清积分；紧凑配置异常的历史状态处理也没有扩大修复。当前作用范围由 `host-review-summary.json` 与独审报告列明。
- 软件与资源快照（非产品容量或时序契约）：85 项 host 测试通过，含 72 个真实拒绝入口的启动/捕获/锁定及 strict/debug 场景、8 个显式控制或配置异常对照、既有完整 C Domain suite 和原 replay 测试。旧生产源码的来源、窗口、紧凑 BAD_FRAME、delay-generation 四项反例失败保留，debug 对照通过；两次 activation 夹具错误及修正也保留。A/B Release 和 Flash 链接通过，静态 RAM 尾端、堆及两核栈边界均不变，Flash 各增加 392 B。
- 当前源码首次四板 quick P3 严格通过，passed/strict_gates_passed 为 true、diagnostic_failures 为空，范围为 TDMA-only，DPLL 标记 `SKIPPED_TDMA_ONLY`。源码指纹 `b06f286d6a68a3a3bc069fead718ee89ec236d682d1391a13dd42004ca2ed115`，包 SHA256 `77cf838802cda67006a00b2a80617e001b9ced1a2cc76ab3c7d023d13b82a5f5`；缓存 build 标识仍为 `20260915022619`，不能单靠该标识识别新旧固件。主控核对 source/receipt/OTA/包及原件，未修改 OTA 实现或配置、未操作 NO5。代码与匹配 P3 凭证提交为 `cf4206f`。
- 四板自主输入定位分别在旧固件 `baseline-r1` 和本次新固件 `after-skip-r1` 执行，均先确认全板 STOP，核对 UID、角色及 load mask，再以现有普通 bootstrap/自主 TRIAL 触发。实读四板 load mask 均为 91，VDC/DPLL 已开，角色为一主三从；未因候选配置文件的 mask 数值推断实际未启用。TDMA SRAM 从启动记录，DPLL trace 在预声明自主阶段 ARM，共同观测时段约 4.5 s；期间无查询，全部 STOP ACK 后顺序保存 SD/读回。完整探测流程分别约 33.3/40.4 s，不含构建、OTA 和 P3，适用于快速输入定位。
- 新固件正向原件快照：四板有效 RX 增量分别 1772/1745/1722/1697，坏帧及运输错误增量均为零，三从成对事件无 fault。自主主板采样点的 timestamp resolution 为零、flags 为 diagnostic-only，正式 TX/RX 时间为零；三从本地 latch 有读数，但 observer eligible 增量均为零。四板自主 trace 均为零记录、零 dropped。主板 observer accepted 增量为 162，基线对应为 160；这些计数包含 bootstrap/切换，不能当作自主持续更新。零 trace 不证明精度不合格或 PI 发散，STOP 后 last_result 也不代表 RUN 的拒绝原因。
- 证据目录为 `out/HardwareAcceptance/20260916/dpll-sample-skip-r1/`（`host-review-summary.json`、`implementation-independent-review-r1.json`、`resource-comparison-main-r1.json`、`audit-main-r1.json`、`p3-r1/acceptance.json`）及 `out/HardwareAcceptance/20260916/dpll-input-probe-r1/`（新旧原件、`comparison-main-r1.json`）。旧包/ELF/map 已在 build 前冻结，避免同名增量产物覆盖后混淆基线。生产 API 的保持行为由 host 反例对照证明，四板原件证明当前集成与输入现状，尚未实板注入坏样本。独立原件复核重解 CRC、重组原始 SD/RAM 回复并逐字节核对；文档与源码分离提交。
- 下一 gate：从稳定运行的有效帧直接接通自主 origin 的 reference TX 时间与接收侧时间关联，进入既有特等席及 VDC evidence 路径；确认实际 accepted/DCO 更新后再做实板坏样本保持、真实更新预算、共同时间命令应用及示波器锁相。首档成功、固定连续无错窗口、首 CS 精细证明均不是前置。既有全相位时序失败和正式精度任务保留，不能借本次 P3 通过追认；长期目标保持 active。
