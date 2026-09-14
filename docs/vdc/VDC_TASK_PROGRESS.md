# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-14

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

执行顺序与任务状态统一见 `VDC_DOMAIN_TODO.md` 的“分阶段执行清单”和任务依赖表；
本文仅追加每个切片已经发生的验证、失败和下一 gate，不复制第二份迁移顺序。

`VDC-RESOURCE-001` 当前编译容量的 compact RX 资源切片已闭合，有限采集的 STOP 后
交接通过当前源码四板 quick P3 验证，见下方 `VDC-PROGRESS-20260914-006`。
自主 origin 补测发现时间输入尚未接通，见 `VDC-PROGRESS-20260914-007`；事件与资源
审计见 `VDC-PROGRESS-20260914-008`；原始记录原型、当前容量目标链接及板端预采见
`VDC-PROGRESS-20260914-009`。当前入口仍为 `VDC-TIME-002`，补齐配置组合与 raw
停止/重臂负测后，才开放全窗计时验收。按 `VDC-TIME-001` 至 `VDC-TIME-004` 补齐
`VDC-TDMA-001` / `VDC-EVID-001` 的自主时间戳输入，再推进 `VDC-SCHED-001`、
`VDC-ROLE-001`；全表 WCET 和正式锁相仍未闭合，
命令接线须等待契约独立审核。`VDC-TDMA-001`、
`VDC-CAL-001` 和 `VDC-EVID-001` 继续提供正式 evidence；`VDC-SERVO-001/002` 在
正式 evidence 未闭环前的 host/replay 或板端诊断不得用于发布板端目标锁。

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
