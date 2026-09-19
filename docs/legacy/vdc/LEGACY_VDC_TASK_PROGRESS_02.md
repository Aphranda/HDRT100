# VDC 任务进度轮转归档 02

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md`
Related: `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-18

本文件按 C14 保存最旧连续 checkpoint，以下正文逐字保留。

### VDC-PROGRESS-20260917-010：typed 原生记录与三从真实 DCO 更新

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。新增诊断记录与读回，不改变既有本地控制算法，不授予锁相或正式 VDC 发布。
- 代码与匹配凭证已提交为 `8d69d05`，真实 commit hook 核验当前 staged 源码通过。
- `vdc_priority_trace.h/.inc` 复用既有采集池，记录实际 MATCH 区间、源事件、运行代际和
  DECISION 的真实 DCO 前后状态；MATCH 按 raw 事件时间有界降采样。Core0 仅提交停止态
  请求，Core1 处理 ARM/STOP/RELEASE 并确认；冻结记录由读取 lease 保护，全部 STOP 后
  分页核对 payload/file CRC。新旧采集互斥，未完成读回不自动释放原件。
- 独审发现并修复两处交接风险：RELEASE 的 published ACK 先于实际归还可能造成后继
  命令无人消费；typed 覆写后未退休旧 completed 元数据可能被旧 READ/SAVE 错标为旧格式。
  真实原子写入交错、旧函数失败/修复后通过及跨格式负例均保留。相关回归 523 项、新记录器
  24 项通过；不是用 mock ACK 或独立测试数组替代真实 owner/共享存储。
- Release App A/B/Boot、Flash link 与独立 linked 资源审核通过：净增静态 RAM 388 B，
  主 RAM 余 38020 B，仍只有原 7600 B 采集池；新增完整 trace service/match 路径含 IRQ
  和异常帧为 584/576 B，既有最大启用路径仍为 2872/3072 B。可选 TRIGGER_MEASURE/MODEL
  保持关闭，不据静态栈结果宣称 WCET 或运行水位通过。首轮 PowerShell 日志重定向返回值
  与已完成构建不一致，原日志保留；结构化 subprocess 复核 returncode=0。
- 同源码四板 quick P3：`PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/14/0/0，
  build_id=`20260917004302`，源码指纹
  `d468840dbf3f823d5cd619ca0664d94b3835011d05170c1ff287832cb4caeefe`；包 SHA256
  `0a0668a2244bfe0f25eb59c86c0cbaaef5f6d17a927ea13716d23d2a4ef6492a`。
  保持固定范围、复用已测线序，严格质量告警单独保留。
- `trace-short-r1/` 八秒静默专项通过，运行零查询、无诊断输出。NO2/NO3/NO4 分别保存
  35/36/35 条 MATCH 和各 7 条 DECISION，覆盖 7.664/7.750/7.570 秒；记录无丢弃，
  三从的 ARM/STOP/RELEASE 均获 Core1 ACK，原件分页和整文件 CRC 通过。

| 板卡 | 真实采用次数 | 零调整次数 | DCO 序号 | 实际频率修正 |
|---|---|---|---|---|
| NO2 | 6 | 1 | 1→7 | 0→+1257 ppb |
| NO3 | 3 | 4 | 1→4 | 0→+157 ppb |
| NO4 | 6 | 1 | 1→7 | 0→+3025 ppb |

- 上表为该轮快照；末次原生决定、FOLLOW 保留快照及真实 Domain 模型逐字段一致。NO2/NO3
  现在已有真实更新证据，不能继续把此前零调整解释为未接入，也不能将本次更新次数归因于
  控制算法修复：本切片没有改变控制律，重新部署/启动与输入条件均与上轮不同。
- 区间分析显示模型残差仍持续下降，未收敛。单点 residual 全宽为本地投影宽与远端宽之和：
  NO2/NO3/NO4 分别约 4.102–6.930 / 5.594–8.634 / 4.282–7.710 µs。NO3 的本地 raw
  起点窗口为 707 tick，另两从为 334 tick；后续投影还引入 bridge 和量化不确定性。
  当前约一秒估计采用两个端点的独立上下界，no_adjust 也更新基线，不会积累更长窗口来缩界。
  同本地模型段端点分析仍能识别负漂移，见 `analysis/analysis.json` 和
  `analysis/typed_residual_dco.svg`；图保留上下界，不用中点拟合代替物理频差或锁相证据。
- 同 epoch 本地起点虽可作为公共潜变量，当前接口未表达相关性；每次 bridge、NO1 逐帧
  latch 重装与远端模型变化不能直接抵消。原生记录未含完整 bridge 三元组或远端分解，
  不能从总宽反推出唯一原因。下一 gate：继续 `VDC-FAST-003`，先证明时间输入/投影缩界的
  数学与身份条件，选择最小切片并重复 host→Release→四板 P3→原生专项，比较频差区间和
  漂移；不能削掉上下界或强迫非零修正。物理输出模型采用、相位/100 ns、ACK、单圈期限及
  VDC 正式发布仍未验收。
- 原件根目录：`out/HardwareAcceptance/20260917/dpll-priority-trace-r1/`，包含固定验收计划、
  `code-review.json`、`resource-review.json`、`resource-independent-review.json`、`p3/`、
  `trace-short-r1/` 与全部命令日志。新测试及旧真实函数反例在同日 `typed-trace-host-r1/`。
  独立 `hardware-review.json` 完成原生字节、分页 CRC、区间算术、真实 DCO、身份、
  零 RUN 查询和 STOP/恢复核验，结论 `PASS_SCOPED_TYPED_NATIVE_TRACE_SLICE`。
  四板最终 STOP，临时 FOLLOW/SYNC/MATCH 与 TAP/会话/负载配置恢复；不操作 NO5，未修改 OTA。
  保留原有 `tdma_flight_engine.c` 工作区改动，代码与文档分离提交。

### VDC-PROGRESS-20260917-009：诊断输出扰动、示波器窗口失败与静默恢复

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。固件维持 `6261640`，无新增生产代码，
  不重跑 P0T/OTA。以下数字为本轮证据快照，不能作为频差或精度事实源。
- 复用已验证 Rigol 脚本及四路接线，探头均为 1×。`follow-scope-r1/` 在示波器设置阶段
  请求水平偏移与读回不一致，未打开板端串口；`follow-scope-r2/` 改居中窗口，但较长
  时基下 SING 后未观察到新的 WAIT，未 START，四板已 STOP、输出关闭；根因未归定。
  保留两次失败，不通过加长等待或沿用旧 STOP 状态冒充新采集。
- `follow-scope-r3/` 恢复既有短时基，NO1/CH1 上升沿触发，完整导出四通道 RAW，
  各 10M 点、20 ns 间隔、200 ms 窗口。TRIAL 应答在主板输出请求后约 656 ms 返回，
  早于预设首次输出；运行中没有板端或示波器查询，全部 STOP 后才导出。CH1 有 17 个
  有效上升沿，CH2–CH4 在该窗口内均无有效边沿；三从诊断输出末态 `last_error=6`
  表示调度尝试失败。稀疏输出还存在较大时序跳变，不能把描述性直线拟合当作晶振频差。
- 同轮 FOLLOW 专项失败：三从分别完成 3/2/2 次零调整后，末态 reason 为 BINDING；
  observer reason=`TDMA_EVENT_PRE_FAULT`、fault=`TDMA_EVENT_FAULT_OVERFLOW`，
  对应 DMA 环覆盖门禁，不能误称 RXSTALL。完整导出不代表有效闭环测量，零边沿也不代表
  无需调整。当前 phase-only 输出没有每沿模型/回退身份，不作为锁相判据。
- 关闭 SELFTEST 后，以原脚本完成 `follow-after-scope-r1/` 八秒静默对照，重新通过。
  三从各完成 7 次区间跨零的合法零调整，末态 FOLLOW reason=STOP、observer reason/fault
  均零，joined=8119/8120/8121；NO4 保持 DCO 序号 7、+545 ppb。对照支持停止使用当前
  有扰动的诊断输出，不证明其内部具体延迟根因已经修复，也不证明频差已经收敛。
- 原件位于 `out/HardwareAcceptance/20260917/dpll-priority-follow-r1/` 下上述目录；
  `capture_follow_scope*.py` 保留各版本，`scope-capture-review.json` 记录执行前独审。
  `follow-scope-r3/scope/analysis.json`、`slopes.svg` 为辅助输出分析，不能作为锁相证据。
  四板最终 STOP，临时 FOLLOW/SYNC/MATCH/SELFTEST 关闭，原配置恢复；示波器 STOP 并恢复
  EXT/NORM。原有 `tdma_flight_engine.c` 工作区修改未暂存或覆盖。
- 下一 gate：继续 `VDC-FAST-003`，先给 typed 匹配/频率判定接入有界 SRAM 原生记录，
  复用现有采集容量或互斥存储，记录源事件、代际、残差区间及实际 DCO，STOP 后统一读取。
  不依赖旧 `dpll.update_seq` 变化，不引入逐次 SCPI 或额外诊断脉冲。按独立切片完成
  host、Release、资源、同源码四板 quick P3 和专项，再判断宽区间需要优化时间输入还是
  估计窗口；物理输出精度仍单独验收，不能通过删掉区间上下界强制产生 DCO 修正。
  具体候选边界写入同目录 `next-typed-trace-plan.json`，是下一切片草案，尚未实现或冻结为契约。

### VDC-PROGRESS-20260917-008：typed 本地频率控制与真实 DCO 采用

- TODO task ID：`VDC-FAST-003`；父任务仍 IN PROGRESS。以下数值为本轮证据快照，非产品
  容量、时序或精度契约。完成互斥 typed 本地频率控制切片，不代表三从锁相。
- 代码、测试和对应四板凭证提交为 `6261640`；暂存源码指纹及真实 commit hook 均核验通过。
- STOP 配置 `VDC_BOUNDARY_MODE_TYPED_FOLLOW`；MATCH generation 与 mode/session 在同一
  请求元组发布。Core1 只消费本服务真正匹配的新事件，在 committed-model guard 外准备
  同生命周期、同本地模型的两个事件区间，guard 内复验身份、年龄、实际模型与 DCO 后，
  调用既有 Domain 连续调频入口。频率误差从时间间隔比计算并向外取整，负反馈按
  `vdc_priority_follow.h` 的窗口/死区和既有单步/Domain 总限幅执行；零跨越区间不强制调节。
  自身更新后重新建立基线；STOP、换代及陈旧取消，BUSY/缺帧不制造样本。
- 最终相关 host 回归 498 项通过；Release A/B/Boot 与 Flash link 检查通过。独立资源
  审核静态 RAM 增加 960 B，主 RAM 余 38408 B；新增 prepare/apply 完整调用链含 IRQ/异常
  帧 544/768 B，既有最大启用路径 2872/3072 B 未增加，可选 TRIGGER_MEASURE 保持关闭。
  `VDC-PRIORITY-01` v4 经 C11 独审批准，保持 pending；不据资源审核宣称 WCET 达标。
- 同源码四板 quick P3 位于 `out/HardwareAcceptance/20260917/dpll-priority-follow-r1/p3/`：
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/12/0/0，复用既有线序，固定范围不扩大。
  build_id=`20260916232405`；源码指纹
  `526b8ff77416c3ca8972b90c17bc7de61c29b2795025ba0fdc42cba84bd2670e`，包 SHA256
  `5e33501aafa0f94e4767a9e411915e6d4b298962841ca2b141d4188c880a40d9`。严格质量告警仍保留。
- 首次专项 `follow-short-r1/` 在 START 前 FOLLOW 查询超时：临时脚本只登记无符号标量，
  公共串口读取器仍把独立 `1` 当 ACK 丢弃。真实读取器离线复现此行为后，以独立
  `capture_follow_r2.py` 登记布尔响应；原脚本、失败报告不改写，固件和 P3 指纹不变。
- `follow-short-r2/` 八秒静默专项通过。NO2/NO3 各完成 7 次合法零调整，实际 DCO 序号
  仍为 1、rate 为零；末次频率区间分别为 [-8175,4291]、[-6443,3112] ppb。跨零区间
  只能支持“不确定修正方向”，不能支持“无需同步”或已经锁定。NO4 完成 6 次真实采用、
  1 次零调整，末态 DCO 序号 7、rate +545 ppb；末次区间 [-8731,-216] ppb，修正 +54 ppb，
  序号 6→7、rate +491→+545 ppb，与 Domain 末态一致。独立整数重算与原始快照一致。
  运行零查询，全部 STOP 后读回并恢复 FOLLOW/SYNC/MATCH、TAP、会话和原有负载配置。
- 证据入口：上述目录中的 `acceptance-plan.json`、`code-review.json`、`c11-review.json`、
  `resource-review.json`、两轮 `input-probe.json` 及 `actions.jsonl`；软件日志为同日
  `priority-follow-host-final.log`，构建目录 `out/build-priority-follow-r1/`。
  独立 `hardware-review.json` 的 72 项原件复核通过，含源码/包/凭证、四区间任意精度
  重算、实际 DCO、运行零查询、STOP 及恢复；补录 wrapper 与 observer helper 的 SHA。
- 下一 gate：继续 `VDC-FAST-003`，观测实际漂移并缩小频率估计不确定性。现有 internal
  capture 按 `dpll.update_seq` 变化取样，typed 控制更新的是 `dco_update_seq`，不能据旧
  trace 推断新路径未执行或全程收敛；先复用四通道触发波形或独立完善板端记录。
  GPIO 模型采用、斜率改善、ACK、相位控制、单圈期限、100 ns 精度及 VDC 正式发布仍未验收。

### VDC-PROGRESS-20260917-007：observer DMA 排水修复与三从有限窗口持续匹配

- TODO task ID：`VDC-FAST-002`、`VDC-FAST-003`；父任务仍 IN PROGRESS。以下数值均为本轮
  快照，非产品事实源。本切片解除本地事件 FIFO 排水阻塞，尚未接入 typed 本地 DCO。
- 代码、测试及对应四板凭证已提交为 `102bbd7`；`check-staged`、手动及真实 commit hook
  均核验通过。独立资源、软件和两轮硬件证据复核未发现本切片阻断；文档分离提交。
- TDMA owner 为三路 observer 配置固定 SRAM DMA ring，Core1 在既有 TDMA/VDC/DPLL
  边界有界消费；history/feed 仍由 Core1 前景单写。DMA 使用 NORMAL 有限完成计数，
  不使用 RP2350 ENDLESS 模式下不会递减的 `TRANS_COUNT`；整圈覆盖、复制后覆盖、
  总线/计数异常均使 epoch 失效。STOP 先停 SM，再有界停 DMA；超时保留 channel/ring，
  阻止 persona/FIFO 复用并允许重试。DMA 不凭空 FIFO 或稳定计数声明无在途旧词。
- 中间写指针版本在 `observer-dma-fix-p3-r2/` 通过基础 quick P3，专项
  `observer-dma-pointer-match/` 三从匹配 53/28/6 次后退休；不能作为持续性成功。
  最终版本改用完整完成计数并按 DMA 容量设置 join 等待窗口，保留严格超时退休，
  未采用同 epoch 清 pending 后继续的方式。原失败和中间证据不删除、不追认为最终凭证。
- 软件：真实 device DMA block/STOP helper 提取并链接真实 observer 的 28 项通过，
  覆盖 full/multi-wrap、复制竞争、计数模式/耗尽、claim 回滚、STOP 超时/重试及在途旧词；
  observer 回归 24 项、matcher/priority RX 回归 264 项通过。Release A/B/Boot 与 Flash
  link 检查通过。资源复核主 RAM 余 39368 B，净减少 772 B；observer 最深整合栈
  2592/3072 B，既有 follower/NO1 路径无新增超限，可选 TRIGGER_MEASURE 仍禁用。
- 当前源码四板 quick P3：`out/HardwareAcceptance/20260917/observer-dma-normal-p3/`，
  固定 P0/P3/T3/TDMA，复用既有线序并核对 UID/build；耗时约 200 s，
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/15/0/0。原严格 TDMA
  `passed/closed_loop_passed/realtime_gate_passed=false`、启动 barrier 超时与诊断继续
  原件保留；快速分级通过不等于严格质量通过。build_id 仍为 `20260916224543`，
  新包 SHA256 为 `e6c5041cac94dfad21d01c422a5229147487ae8d706085c9fa35d558ee6cc759`，
  当前源码指纹 `bab6480d7031cdbe3c0d98e7958fa1b1f2020ee0a13d302cb292d855cd3c17b8`；
  必须用当前 receipt，不能仅以相同 build_id 复用中间版凭证。
- 同源码静默专项 `observer-dma-normal-match-r1/`（3 s）和 `-r2/`（8 s）均通过；
  运行查询数均为零，全部 STOP 后读取并恢复配置。三从成功匹配数分别为
  103/99/68 与 237/273/198。第二轮 observer joined 为 8094/8094/8095，末态
  reason/fault 均零；三从最后成功匹配事件均为 8093，与最新 retained RX 相同。
  matcher reason=STOP 是正常停止退休，不是运行中失效。启动 SEQUENCE 恢复记录仍保留，
  不能宣称全程无错、每帧匹配或单圈期限已达标。
- 证据：`observer-dma-resource/resource-review-final.json`、`match-continuity.json`
  与两轮 `input-probe.json`；测试原件 `out/pytest/event-dma-main-r1/`、
  `out/pytest/observer-dma-final-main/` 及同日 `observer-dma-*-host.log`。实测最大服务
  间隔/耗时仍需后继预算优化；保守 age 下界也未证明长期 epoch 无限存活。本轮残差含
  本地未闭环绝对偏移，不应当作链路 delay 或锁相误差结论。
- 下一 gate：进入 `VDC-FAST-003` 的独立 typed 本地 rate estimator/DCO 切片，复用
  `dpll-priority-match-r1/next-dco-plan.json`。从两个同生命周期有效事件形成带单位的
  ppb 区间，以互斥模式在 Core1 committed guard 内复验并应用真实本地 DCO；不将 ns
  residual 直接当 ppb，也不伪造远端 model token。随后独立测试、Release、四板 quick P3
  和真实 DCO/斜率专项；不重做 P0T，不将启动首帧和 100 ns 精度反向设为当前前置。

### VDC-PROGRESS-20260917-006：精确源事件匹配与本地观测 FIFO 阻塞

- TODO task ID：`VDC-FAST-002`、`VDC-FAST-003`。
- 状态：IN PROGRESS。Core1 已在 committed-model guard 前接入 typed live handoff：按完整
  source event sequence 直接索引历史槽位，复验 ARM/observer epoch、ordinal、完整序号、
  TIMER1 enable bracket、模型生存期和 RX 代际；用实际 committed DCO 投影计算 local 区间，
  加已装载 reverse-DATA 矩阵转置得到的暂定正向 CS delay，输出 signed residual。此切片不发
  DCO、ACK 或锁相命令。
- 软件/资源：exact、matcher、SCPI、wrapper fixture 相关独立回归分别通过；Release A/B/Boot
  通过。独立资源复核记录主 RAM 余量约 40140 B、matcher 完整 DPLL 相位栈约 1204/3072 B，
  既有最坏 follower/NO1 路径无回归。精确查找不扫描历史、不动态分配、不屏蔽 IRQ；matcher
  workspace 和快照静态分配。`next-dco-plan.json` 仅为后续只读方案，未接入 DCO。
- 四板基础 P3 使用 `QUICK_DIAGNOSTIC`、固定 P0/P3/T3/TDMA 范围完成
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL 为 25/15/0/0；严格调度告警保留。证据根为
  `out/HardwareAcceptance/20260917/dpll-priority-match-r1/p3/`，源码 build 为
  `20260916191056`，不能把基础 P3 结果写成匹配专项通过。
- 匹配专项首轮 `match-short-r1` 保留失败：三从 typed 接收约 3037 条且末条邮箱/CRC 一致，
  但默认 TAP 未启用时本地 observer 分别在 source event 64/57/55 后 INVALID；RECOVERY
  零计数只表示该配置下不记录恢复，不能据此断言具体硬件原因。NO2 成功配对一次，source
  event 64/carrier 66、delay 82 ns 和 residual 算术独立复核；NO3/NO4 未匹配。
- 使用已验收显式 TAP（NO2/NO3/NO4 prefix 120/109/98、delay 15）复测，保留
  `match-short-r2` 与 `match-short-r3` 原始 actions 和 STOP 后诊断。r2 中 observer 均按
  既有恢复路径推进，但 NO1 未形成有效 offer（TX encoded=0），三从只收到约 80 个早期载荷；
  r3 在同源码复位、复用当前矩阵与显式 TAP 后，NO1 形成有效 typed 候选约 605 个，NO2/NO3
  各成功匹配 source event 149，carrier 152，delay 分别 82/164 ns；NO4 未匹配。三从仍
  出现 `TDMA_EVENT_FAULT_STALL`，该位来自 observer SM1–SM3 的 RXSTALL 合并，只能证明
  观测器组至少一 SM 溢出，不能归因 DATA RX DMA。r3 recovery reason=SEQUENCE、fault=1
  的原始证据保留，专项仍 FAIL。
- 根因方向已收窄：每帧 observer RX/TX 计数器写两个字，JOIN_RX 8-word FIFO 约容纳四帧；
  TDMA phase 之间服务间隔过长时 FIFO RXSTALL，继续使用会破坏时间轴，observer 正确退休。
  下一切片评估在 TDMA owner 内固定容量的原始 batch 暂存/前景单写者消费，或缩短观测服务
  间隔；必须保留溢出、epoch 失效和固定容量证据，不能清 sticky 后冒充连续时间。不要把
  载荷持续接收误判为本地事件观测或 DCO 闭环完成。
- 后续只读 DCO 设计见 `next-dco-plan.json`：typed 输出无 remote model token，不能把
  generation/event sequence 伪造成 token；须先完成三从稳定 residual，再增加互斥 typed-follow
  mode、Core1 rate estimator 和有界本地 DCO commit。首帧/P0T/100 ns 精度不是当前 FIFO
  阻塞的前置条件。
- 下一 gate：`VDC-FAST-002`，独立设计/资源审查后实现 observer raw harvest 或等价有界
  服务修复，重新编译并运行匹配源码 quick P3；之后才重跑三从匹配专项并进入 `VDC-FAST-003`。

### VDC-PROGRESS-20260917-005：同步记录直接交接与 IRQ 热路径收敛

- 代码与匹配四板 P3 凭证已提交为 `c144ad8`，`check-staged` 与真实 pre-commit
  已核对本轮源码指纹通过；文档分离提交。
- 对应 `VDC-FAST-001/002`。本轮先复核上一实板原件：三从 priority RX 已发布
  2644/2644/2646 条，而 DPLL phase 的 latest-only 诊断仅复制 270/269/267 条。
  主要缺口在消费者节拍，不能称为物理链路丢帧。STOP 后读取旧固件的分段计时，
  确认复制、校验、发布多段都有成本；CRC 内核和环形复制原本已在 RAM。
  以下计数、时长、资源均为本次快照，非产品事实源。
- 新增 STOP 配置门禁注册的固定 RX sink。TDMA 在成功校验/发布后，立即在 Core1
  IRQ 同步交给 typed 定长解码入口；不等待 DPLL phase，不执行 PI、通用解析、
  存储或 RTOS。保留原慢速轮询作对照，独立状态避免 IRQ/前景多写者。载荷圈回绕
  交接使用发布后 epoch；STOP、DMA 坐标退休和 epoch 耗尽撤销入口。同身份却
  时间区间不同拒绝为冲突，普通启动载荷、typed 成功、重复、相邻不同事件分开计数。
- IRQ glue、候选定位、验证、发布、CRC 外层、小型 codec/sink 和 watchdog 标记
  迁至主 SRAM；保留全部 CRC 与身份检查，消除随后会被完整覆盖的工作记录清零。
  未扩大候选 IRQ 预算，未修改 OTA 实现。新明细 `SYSTem:VDC:PRIORity:RX?`
  只在 STOP 且入口已退休时读回；该诊断 getter 不授予实时控制消费资格。
- Host：owner/原 ingress/codec 89 项、SCPI/发送回归 267 项、直接 sink 92 项通过；
  后续 owner 4 项复核通过。独立方另复核 97 项。A/B/Boot Release 与 Flash link
  检查通过。独立 A/B map/dis/ELF 核验：主 RAM 扣预留 heap 后 41828 B，较上一
  切片减少 4128 B；scratch gap 仍 24 B。新 IRQ 最深软件栈 200 B，follower 完整
  前景加 IRQ/异常帧 2708/3072 B，NO1 2872/3072 B。此为已审四板 persona，
  不是全局栈或运行水位证明。VDC-PRIORITY-01 v2 保持 pending，独立 C11 已批准该范围。
- 四板固定范围 quick P3 一轮完成，耗时约 192 s，`PASS_WITH_WARNINGS`；
  INFO/WARN/ERROR/FATAL 为 23/20/0/0，DPLL 专项 SKIPPED。严格质量仍未通过：
  TRN-01/SCK re-arm 裕量及 TDMA startup stable barrier 失败原件保留。复用已确认
  线序，没有重新 P0 寻序。build `20260916183948`，1208 个源码文件，指纹
  `2cc7f6ea4af4d49ef27d9e414bf657f2fd410754d6fd1ae3a750a7641224049a`。
- `direct-short-r1` 三秒静默专项 PASS，零 RUN 查询，最终四板 STOP 并恢复
  load/diagnostic/burst，禁用同步与 session，撤销临时 trial。三从直接入口收到
  2961/2963/2963 条，与各自同 epoch 的 TDMA 成功发布数严格相等；其中 typed
  各 2902 条、普通启动 59/61/61 条，typed 拒绝和冲突均零。相邻不同源事件各
  749 个、重复各 2153 次；NO1 本轮生成候选 749 个，计数一致不替代完整逐事件
  线上追踪。三从末条 typed 与 NO1 同事件 2947、carrier 2965，区间和 CRC 均对应。
- IRQ body 的三从平均 cycles 从约 28103/28324/28117 降至
  3787/4140/4115；最大从 50706/48485/46334 降至 4496/5187/4724。
  按 `BOARD_SYS_CLOCK_HZ` 换算，平均约 112–113 us 降至 15–17 us，最大约
  185–203 us 降至 18–21 us。四段 maxima 不相加，body 之外仍有 IRQ 进出与
  finish/accounting tail；不同轮次不能作为硬 WCET 证明。仍超过
  `PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES` 候选预算，严格相位预算与物理同圈
  截止时间未闭合。原 poll 本轮仅复制 185/179/120 条，不再代表直接入口数量。
- 证据根 `out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/`：
  `stopped-baseline.json`、`irq-cost-review.json`、`implementation-host-r1.json`、
  `owner-host-r1.log`、`owner-final.log`、`integration-host-r1.log`、`build-r1.log`、
  `code-review.json`、`resource-review.json`、`c11-review.json`、`hardware-review.json`、
  `p3/acceptance.json`、`direct-short-r1/input-probe.json` 与 `actions.jsonl`。
  `capture_direct_rx.py` 经主控审阅后用 Windows 原生 Python 串口入口执行，
  复用已有配置/STOP/helper；未改变验收程序或当前源码指纹。
- 独立实板结论 `PASS_SCOPED_FOUR_BOARD_DIRECT_TYPED_RX`：54 处证据/源码哈希、
  最终 A/B ELF/map/dis、P3 分级及 211 条原始操作记录核对通过；三从原始 header
  身份/transport CRC32 与 mailbox CRC16 均重算通过，无本切片提交阻断。
- 下一 gate：直接入口已消除已准入记录等待 DPLL phase 的交接缺口；继续核对
  物理帧到入口期限及 IRQ 预算；`VDC-FAST-003` 已整理按 source event
  精确索引与本地 delay/模型残差方案。不得把原 poll 诊断当作新通道丢帧，也不得
  把收到/解码当作从板 DCO、ACK 或锁相完成。计划见 `next-event-match-plan.json`。

### VDC-PROGRESS-20260917-004：Core1 同步发布与三从真实接收

- 代码与匹配 P3 凭证已提交为 `686e2db`，暂存源码指纹已由 `check-staged` 和
  真实 pre-commit 核验通过。文档分离提交；文档检查器与 18 项自回归通过，既有
  `TDMA-FLIGHT-BITMAP-01` 登记格式 WARN 保留。
- 对应 `VDC-FAST-001/002`。本轮代码把已提交 DCO 的真实 origin 事件区间在 Core1
  编成 typed 邮箱，TDMA owner 直接交给既有 DMA exchange 双缓冲；不经过普通
  FIFO/overlay 的 Core0 每事件准备。STOP-only generation、完整运行绑定、模型
  生效时间、重复事件冻结和区间溢出均检查；普通异步工位可取消但不等待其 ACK
  才发送，工位 ACK 前不复用。物理 bank 仍由真实 selection 退休。
- 字段及边界登记为 `VDC-PRIORITY-01` pending，独立方 `p0_root_review` 完成 C11，
  见 `VDC_CROSS_REVIEW_04.md`。先前“等待普通取消 ACK”、STOP 注册竞争和普通同步
  fallback 类型放开三项发现均已修复；同期修正 codec 区间溢出检查和旧测试中
  已启用 class 被误列为 forbidden 的断言。失败原件保留，不追认为测试通过。
- 以下数值均为本轮快照，非事实源。provider 的实际生产函数 host 252 项通过，
  codec+provider 合计 253 项通过；集成 priority RX/ingress、物理邮箱、SCPI 共
  110 项通过，完整 adapter C 回归通过。A/B/Boot Release 与 Flash link 检查通过。
  独立资源复核绑定最终 ELF/map/dis：主 RAM 保留 heap 后 45956 B（比上一切片少
  772 B），scratch gap 24 B；新 provider 链含 IRQ/异常最大 1776/3072 B，原 NO1/
  follower 最大路径仍为 2872/2716 B。仅覆盖已审四板 persona，不授予全局栈或 WCET。
- 证据根 `out/HardwareAcceptance/20260917/dpll-priority-tx-r1/`。首轮 `p3/` 在
  TRN-00 发车前的 NO3 `TOPology 4,2,0` 返回 `<timeout>`/`-200`，没有训练观测；
  失败仍保留。用相同固件与 OTA 原件 `resume` 后 `p3-resume/` 固定范围
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL 为 24/27/0/0；严格质量未通过，
  DPLL 专项在该 P3 中 SKIPPED。复用确认线序，没有重跑 P0 扫描或更改 OTA。
  build `20260916181047`，1205 个源文件，指纹
  `f30660c864bd307471a08c8a689b04c80f64140742baa466b7f5b73ca3ab6b18`。
- 短时实板专项首轮 `typed-short-r1/` 在 START 前被旧采集 helper 的 LOAD:MASK
  复合 ACK 过滤阻断。仅修 out 采集脚本的 ACK 注册后，`typed-short-r2/` 成功；
  复用当前板上矩阵，四板 STOP/UID/build 屏障、显式 session/generation、ARM/START、
  有限自主 trial、静默运行及最终 STOP 后读回全有原始 actions，未在 RUN 查询采样。
  最终四板停止，开关禁用并恢复原 load/diagnostic/burst，trial 已撤销。
- NO1 生成 684 份编码候选；该计数不是物理提交或送达总数。三从 Core1 原始记录
  消费累计分别为 270/269/267，包含启动普通记录，不能称全部为 typed 次数。
  三从末条均为 class `0x14`，完整载荷圈序号 2648；body 指向源事件 2645、运行
  generation 1789582893，纳秒下界 142840259535，width 1687。三从末条 32 B
  邮箱均与 NO1 保留候选逐字节一致，header 身份 CRC 与邮箱 CRC 独立校验通过。
  载荷圈与事件相差不等于测得运输延迟；本轮尚无逐圈最晚到站证明。
- NO1 已提交 DCO 序号从 456 至 543、model token 从 1 至 88；此为主板模型确实
  在更新的快照，不代表 typed 从板控制已经接入。当前新通道只运输/解码保留，
  NO2–NO4 尚未在此路径按同事件、本地 delay 应用 DCO。width 保留投影与计数
  不确定度，不能宣称 100 ns 或共同时间锁相。
- 主要原件：`provider-delivery.json`、`provider-host-r1.log`（旧失败）、
  `provider-host-r2.log`、`integrated-host.log`、`build-final.log`、
  `owner-review.json`、`priority-tx-resource-review.json`、`c11-review.json`、
  `hardware-review.json`、
  `p3-resume/acceptance.json`、`typed-short-r2/input-probe.json` 和 `actions.jsonl`。
  硬件辅助入口 `capture_typed_tx.py` 由主控审阅后以 Windows 原生 Python 执行，
  复用既有串口 helper；旧脚本保存为 `capture_typed_tx_r1.py`。
- 独立硬件复核为 `PASS_SCOPED_FOUR_BOARD_TYPED_TRANSPORT`，无本切片提交阻断：
  核对 37 处引用哈希、源码指纹、四板 UID/build 及 196 条操作记录；三从 transport
  CRC32、immutable identity CRC32 和 mailbox CRC16 均独立重算通过。四板 STOP
  有确认，运行采集期间没有查询；结论仅覆盖本次真实 typed 运输，不提升为严格
  P3 质量、单圈截止时间、从板 DCO 应用或锁相通过。
- 下一 gate：继续 `VDC-FAST-001/002` 的单圈期限、消费者能力和接收端运行绑定；
  再按 `VDC-FAST-003` 直接索引同一源事件、加本地已测 delay 并实际应用 DCO。
  三从收到真实同步数据这一缺口已闭合；不回到 RefMem 分片作为实时前置。

### VDC-PROGRESS-20260917-003：P0 瞬态拒绝恢复与 typed 同步接收切片

- 代码与匹配凭证已提交为 `e6861f8`；`check-staged` 和真实 pre-commit 均核对本轮
  源码指纹通过。文档独立提交，文档检查器与 18 项自回归通过；既有
  `TDMA-FLIGHT-BITMAP-01` 登记格式 WARN 保留。独立代码审查记录为
  `typed-ingress-code-review.json`，限定本轮 body 解码保留范围，无提交前阻塞。
- 对应 `VDC-FAST-001/002`；用户要求先修 P0，再回到同步特等席。以下次数、资源、
  build 和统计均为本轮快照，非产品事实源。未修改物理接线或 OTA 实现。
- 纠正前述故障归因：r1/r2 均在下一候选的 `SYST:TDMA:RING:TOPology` 配置阶段
  收到 `<timeout>` 和错误队列 `-200`，尚未开始该候选发送；不是最后显示的成功边失效。
  P0 邻接判据采用 DMA 活动，探测流 `rx_frames=0`/`magic_fail>0` 不等于该判据失败。
  NO2→NO3 原样定段复测 16 轮中 15 轮成功，失败轮反馈 session 为零且 STOP
  config/applied 均为 154；因此不能归因于活动反馈会话或 RJ45 被移动。
  具体固件拒绝分支（快照、锁、待处理意图或 STOP 退休等）仍未知。
- `configure_pair_topology()` 复用 `_set_stopped_topology()` 的既有有界恢复：
  清楚记录错误队列基线与 session，只有已知瞬态拒绝可有限重试，每次重新证明 STOP，
  必须收到精确 topology tuple 和新的 applied generation 后才 ARM。未知超时、
  持续拒绝、活动 session、旧 generation 均失败；原快速等待参数和邻接判据不变。
  已移除早期逐对清 session 的尝试；该命令的 `"OK",0` 被通用读取器拆成 `,0`，
  不能把当时记录的 `<timeout>` 当成有效 ACK，也不据此宣称板端拒绝清会话。
- 修复后同段 16/16 轮成功，其中一次真实首尝试拒绝经有界恢复成功，四板清理均通过。
  修复前后原件分别在 `out/HardwareAcceptance/20260917/p0-pair-repeat.json` 和
  `p0-pair-repeat-fixed.json`。完整失败保留在 `dpll-typed-sync-r1/p3/p0t-topology/`
  与 `dpll-typed-sync-r2/p3/p0t-topology/`；本次恢复不能追认旧轮通过。
- 主线新增独立同步类 `TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS`、
  定长 `VDC_PRIORITY_CODEC_BODY_SIZE` body 编解码及 Core1 有界 typed 解码保留。
  普通 process-image whitelist 不变；真实邮箱外层 source/target/header/CRC
  仍由 TDMA priority validator 检查。codec 的 CRC helper 仅覆盖 body，不替代邮箱 CRC。
  成功/拒绝计数保留在内部快照，未新增 SCPI 输出；无效 body 不覆盖上次解码记录，
  重复 carrier 不重复解码，换 ARM epoch 清理状态。记录未用于匹配或 DCO 授权。
- Host：P0/calibration 三组 97 项通过；codec/priority RX/Core1 ingress 三组 89 项通过。
  本轮修复了 ingress 测试夹具漏链接生产 codec 的错误，并增加 typed 成功、拒绝、重复、
  普通类隔离及 STOP/ARM 用例。A/B/Boot Release 与 Flash link 检查通过。
  独立 A/B 资源复核：主 RAM 保留 heap 后 46728 B（比前一切片少 64 B），scratch gap
  24 B；既有 follower/NO1/DPLL Core1 路径分别为 2716/2872/2012 B，未增长。
  仅为已审正常 persona 的静态路径分析，不代表全局栈、运行时水位或 WCET 证明。
- 当前源码四板 P3：build `20260916174836`，1200 个源文件，指纹
  `b928dbe4460a4ee49493438a016e5caa3ddeb7cb46caee2431196b9047ed95ef`。
  固定 `QUICK_DIAGNOSTIC` 范围 `PASS_WITH_WARNINGS`，P0/P1/P2/P3/T0 为 PASS，
  T1/T2/T3/TDMA 为 PASS_WITH_WARNINGS，DPLL 为 SKIPPED；INFO/WARN/ERROR/FATAL
  分别为 30/24/0/0。完整环序仍为 NO1→NO2→NO3→NO4→NO1，P0 清理和最终四板
  stopped handoff 有效。原 TDMA `passed/closed_loop_passed/realtime_gate_passed`
  均 false，`diagnostic_passed` true；`strict_gates_passed` false，严格失败保留。
- 证据根 `out/HardwareAcceptance/20260917/dpll-typed-sync-r3/`：
  `p3/acceptance.json`、`p3/alarms.json`、`p3/diagnostic.json`、
  `p3/p0t-topology/summary.json`、`p3/tdma-process-image/summary.json`、
  `p3/tdma-stopped-handoff.json`、`typed-sync-resource-review.json`。
  本轮硬件命令从既有 Windows 原生 Python/PowerShell 会话继续执行，以保留串口流程：
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run --tdma-only --diagnostic-continue --out-dir out/HardwareAcceptance/20260917/dpll-typed-sync-r3/p3`。
- 下一 gate：P0 编排缺口已闭合，回到 `VDC-FAST-001/002` 的 NO1 确定性 typed 发布，
  先明确时间单位、绑定代际的预安装/失效和完整邮箱验证。后续 `VDC-FAST-003` 接入
  Core1 完整事件匹配、本地 delay 与 DCO。当前没有实板 typed TX 数据，不能把
  普通邮箱回归、body 解码或快速 P3 凭证说成同步单圈交付、三从闭环或锁相。

### VDC-PROGRESS-20260917-002：Core1 priority RX 原始记录消费接通

- 对应 `VDC-FAST-002`；本切片只闭合“TDMA priority RX 保全 → Core1 读取”的入站边界。
  `sync_dpll_fb_service()` 在既有 Core1 服务边界执行一次最新候选快照和一次
  `tdma_priority_rx_copy_live()`，使用完整 carrier sequence、epoch、retained 位图和
  guard 复验；失败不退休候选，下一次服务重试。latest-only 的跳过数单独记录，不能解释成 FIFO 丢帧。
- 新增 stopped-only `SYST:TDMA:FLIGHT:PRIOR:CONSumed?` 读回，输出 Core1
  服务/消费/重试/重复/序列间隔/时钟无效计数、有效 body 计时及最后原始记录。
  普通 class（本轮实板读回为 `0x10`）只保留原始字节，不进入 VDC residual、时间戳、ACK、
  DCO 或锁相控制。原 priority `REC?` 的 STOP 诊断读取保持不变。
- live-copy host 组合及 Core1 ingress 组合共 108 项通过、无跳过；覆盖 6/8 节点、
  short-enum、STOP/ARM/rebase、换代、同槽覆盖、guard 竞争、序号回绕、时钟失效和
  普通/未知 class。资源独审绑定 build `20260916162422`、源码指纹
  `2d2163a96166a8f9a7d2faae26fba502d27e02c6db40d27b7451ff9b594772bb`、1197 个源文件；
  A/B/Boot Release 和 Flash link 检查通过。当前 persona 资源快照：主 RAM 保留堆后
  46792 B，follower Core1 路径余 356 B，NO1 路径余 200 B；均为本构建快照，非全负载保证。
- 四板 `QUICK_DIAGNOSTIC` P3 已完成，固定范围结果为 `PASS_WITH_WARNINGS`，TDMA
  原始过程结果和严格质量失败均保留；本轮未执行 DPLL 观察，不能宣称严格 P3 或锁相。
  STOP/UID/build 屏障后，NO2/NO3/NO4 分别出现 244/239/240 次 Core1 原始记录消费，
  最新 mailbox class 均为普通过程类；NO1 未安装 follower priority consumer。消费 body
  最大值和 IRQ-entry→read 最大值见 `consumer-readback-r1.json`，均为当前板端快照，
  不是 WCET 或物理飞行/相位时间。三从读回的 retained record 与 producer exact record
  在仍保留时逐字一致；不在槽位时只保留独立 CRC 结果并明确标记。
- 证据目录：`out/HardwareAcceptance/20260917/dpll-priority-sync-r1/`，包括
  `ingress-test-delivery.json`、`independent-review/linked-ingress-review.json`、
  `p3-r1/acceptance.json`、`p3-r1/alarms.json`、`consumer-readback-r1.json` 和
  `measured-build-r1/manifest.json`。严格门限仍为 false；本切片只证明 Core1 原始
  运输记录消费，未证明同事件关联、预编码时间戳、DCO 更新、100 ns 精度或锁相。
- 下一 gate：继续 `VDC-FAST-002/003`，增加独立 typed synchronization class 和
  NO1 origin 事件发布；随后在同一 Core1 固定索引入口完成完整 event identity、时间区间、
  本地 delay 和 DCO 应用。不得把本轮 `0x10` 普通邮箱记录重新解释为特等席同步记录。
此前 `22a22b5` 已提交参考发布节奏切片与匹配 quick P3 凭证，源码指纹
`3dd87e9aee4b3899f3592c47fb30df3b899dcda290531d4b32514a615785ef98`，
见 `VDC-PROGRESS-20260916-037`。此前 `6530114` 已提交 029–034 的接收 ACK、历史事件桥、本地跟踪、RAM 导出、
等待时间基与桥接窗口修复，以及显式复用已知线序的验收工具和匹配 quick P3 凭证。
该阶段源码指纹为
`f429dde66d6f732160d84930f2b11b14b5b9d853fd023788736d74346100bbeb`，
当时提交与工作源码匹配，见 `VDC-PROGRESS-20260916-036`。此前 `7c46b2f` 只覆盖至
028，`5a22acc`、`643d703` 分别提交后继文档；当时未提交及无新凭证的判断仍是
对应历史事实，见 `commit-consolidation-r1/r2/r3` 的证据。本轮未绕过指纹门禁，
quick 四板流程通过不代表所有实时相位无超限或 DPLL 锁相。

当前执行已按用户最新指令切换到 NO1 自主 PI/发布时间戳、NO2–NO4 接收/环路 ACK、
加本地已测 delay 后本地跟踪，见 `VDC-PROGRESS-20260916-027/028`。
集中式逐从 AUTO 不再作为前置，完整 delay/bias 与物理精度后续验收。
显式 REFERENCE 发布和基础环路 ACK 均已实测，见 `VDC-PROGRESS-20260916-028/030`。
ACK 固件直接四板 TDMA 闭环通过，三从回执与发送末档逐字节对应；该轮三从 DCO
更新均为零。自主阶段 NO1 PI 和三从持续本地跟踪仍待验。完整 P3 的 P0T 准备失败
保留于 029；按用户最新指令，已知线序停止继续探测，校准/工具/SD 问题单列，
只有 TDMA/DPLL 本身阻塞主线。本地事件历史桥的源码、Release 与直接四板 TDMA
回归已完成，见 `VDC-PROGRESS-20260916-031`。集成本地跟踪的软件、Release 与
直接四板 TDMA 已完成，见 `VDC-PROGRESS-20260916-032`；该轮三从无 DCO
更新。等待配对的时间基错配已修复，见 `VDC-PROGRESS-20260916-033`。
桥接采样窗口缩短后，NO4 首次有可追溯的本地 DCO 采用，见
`VDC-PROGRESS-20260916-034`；NO2/NO3 当轮完整参考接收为零，三从持续闭环仍
未完成。035 的停止态 FIFO/RefMem 差分未观察到镜像丢弃；036 的 PHYS 快照确认
三从在 FIFO 前存在大量观察丢弃，并在复位后再次记录 NO4 的真实本地采用。
037 的发布节奏切片已通过当前源码 P3；两轮专项均改善三从完整参考与匹配 ACK，
NO2/NO3 已进入本地控制判断，但误差区间跨零而保持，NO4 再次非零采用。
037 当时下一步为缩窄真实观测不确定度、提高新鲜配对连续性，并核对 OSAL 发布时基；
观察丢弃计数不直接等于缺失分片数，发布间隔不是物理驻留保证，不重跑 P0T 寻优。

以下保留前一路线 checkpoint。显式 AUTO 与 RATE 窗口已接入，初版软件、双槽构建和对应源码严格四板 P3 通过；
自动多轮实板专项仅部分从板响应，尚未闭合。AUTO 专用采集修复的软件及构建通过，
P3-r2 的配置阶段失败保留，随后同源 resume 严格通过。AUTO-r5 证实采集完整，
仍有后继命令未决。Core0 完整结果暂忙交接及 coarse OPMode 维护态工具修复后，
对应源码四板严格 P3 通过，AUTO 完成采集但仍只有部分从板实际应用。后继延后
RX acquire 的修复已完成软件、资源及构建；P0T 启动确认工具修复后，当前源码
四板严格 P3 已通过。后续 AUTO 在启动确认及旧记录交接阶段中止，尚无同源
有效多轮采集；先收敛专项采集入口，再验证运输效果，见
`VDC-PROGRESS-20260916-019/020/021/022/023/024/025/026`。
上一轮源码已完成同命令有界重复运输，软件、双槽构建和当轮严格四板 P3 通过。
同轮专项中 NO2/NO3/NO4 均实际应用一次并返回精确 ACK，重复接收未重复应用；
原始证据独立复核通过，见 `VDC-PROGRESS-20260916-018`。当时自动频率控制尚未
接入，后继方案分析已开始；单次成功不能证明持续锁相。
前轮 017 已修复 TDMA 重配置遗漏继承 active servo CRC，当轮 NO3/NO4 应用及 ACK
成功，NO2 未收到完整命令，该失败原件保留，不由本轮通过追认。
前一诊断切片 016 严格四板 P3 通过：当轮 NO2/NO3 首次拒绝于 APPLY，NO4 没有完整
命令 RX，三从应用均为零。两轮未完整接收的节点不同，运输可靠性继续独立跟踪。
逐从接线见 014，历史三从接收成功见 015；014 的严格校准失败原件继续保留，
不能套用于不同源码的当前状态；当时自动频率控制未接入。
连续重基原语本身的历史严格验收见 `VDC-PROGRESS-20260916-013`，不能用该原语
当时无生产调用的 P3 或 host 数学验证宣称从板闭环。

模型关联反馈已完成当时源码四板严格 P3 与运输/配对专项，见
`VDC-PROGRESS-20260916-012`；独立原件复核通过，代码已提交为 `6716dc5`，当时尚未接通从板控制。
此前准备迁移见 `VDC-PROGRESS-20260916-011`：缓存、配对和差分由 Core0 RefMem
单写者准备，Core1 只发布或退休授权。011 切片当时源码的四板 P3 严格通过，专项完整通过轮次
为 r2/r4；r1/r3 的短 TX 历史缺证保留。NO1 DPLL 超限显著减少，仍未满足局部及
完整静态表预算。当时各从实际 DCO 应用仍为零；后继接逐从专属校正和 Core1
连续重基应用，不能以诊断配对代替闭环。原始配对及运输基线分别见
`VDC-PROGRESS-20260916-010/009`。

最新同条观测留存及本板时间锚见 `VDC-PROGRESS-20260916-008`：严格四板 P3、专项和
原件独立复核通过；首次专项因 NO2 START 缺少真实应答中止，同源新轮次证明三从
恢复后持续留存完整记录，STOP 后历史不变。TDMA 完整相位超限及 RX 覆盖计数仍保留，
不由此宣称完整时序或锁相通过。该历史切片的原始反馈后继见上方当前 checkpoint。
观察器自身恢复见 `VDC-PROGRESS-20260916-007`。NO1 运行中发射记录留存见
`VDC-PROGRESS-20260916-005`：软件、Release、
当前源码四板 quick P3 和 STOP 后同序对账已通过。输出投影准备见
`VDC-PROGRESS-20260916-004`；当时实际 DCO 应用尚未接通。指定主机接收切片见
`VDC-PROGRESS-20260916-003`。目标整理见
`VDC-PROGRESS-20260916-002`，随后贯通各从反馈和专属校正，再消除漂移、收敛精度。

执行顺序与任务状态统一见 `VDC_DOMAIN_TODO.md` 的“当前主线：四板数据运输与真实闭环”
及其中 `VDC-FLIGHT-001` 至 `VDC-RECOVERY-001` 的任务依赖表；
本文仅追加每个切片已经发生的验证、失败和下一 gate，不复制第二份迁移顺序。

当前执行依赖按 `VDC-PROGRESS-20260916-027` 纠偏；此前各记录的“当前”及
“下一 gate”保留历史含义，不将首帧可用、全窗无错或完整绝对时间映射作为运输前置。

### VDC-PROGRESS-20260917-001：阶段告警分级与固定验收范围

- 用户授权按 P0–P3、T0–T3、TDMA、DPLL 的细化表优化验收。新增纯分级策略及主流程接线，INFO/WARN/ERROR/FATAL 与阶段 PASS/PASS_WITH_WARNINGS/FAIL/SKIPPED 分开记录；范围启动前写入 `fixed-scope.json`，`--strict-stage` 显式指定本轮严格目标。
- 快速验收要求实际拓扑、目标档位延迟、矩阵装载读回和逐板有效新帧运输。偶发缺收、启动稳定窗口、调度质量告警单列；缺节点、无有效新数据、STOP 或原生证据失效仍阻断。DCO 零调整不自动报错，未执行 DPLL 为 SKIPPED，不能由 TDMA 运输通过宣称锁相。
- P3 汇总修正非必验 LIMITED_RX 高频探测的范围：原件保留，目标稳定档位及完整频率覆盖仍必须验证，不能拿更多高频探测试验补齐缺失目标档位。纯分级模块不修改原始 passed/失败记录。
- 新快速凭证类型为 `QUICK_GRADED_RECEIPT_SCHEMA`，绑定固定范围、报告、判定输入与原始阶段证据；提交校验重算分级、矩阵读回及 STOP 原生交接。旧凭证兼容，新增证据不可缺失或混配。本轮没有修改固件/PIO/OTA 实现。
- host 组合测试 155 项通过、无跳过；测试最后一轮于上一日启动，证据保留 `out/HardwareAcceptance/20260916/p3-alarm-grading-r1/policy-and-integration-r4.xml`、`test-delivery.json`。独立历史原件与负测不代替当前源码 P3。
- 当前源码四板快速 P3 已于本日完成，运行证据位于 `out/HardwareAcceptance/20260917/p3-alarm-grading-r1/`；沿用已测线序，不重跑 P0T，不操作 NO5。新结果 `PASS_WITH_WARNINGS`：P0–P3/T0–T3 为 PASS，TDMA 为 PASS_WITH_WARNINGS，DPLL 为 SKIPPED。实测报告快照：INFO 25、WARN 20、ERROR/FATAL 均为零；条目含分阶段/逐板与总体归纳，不等于独立故障数量。
- 新凭证绑定源码 `fb2b46871987abe1426a52207966c93942b75fea1a58a53adb396be2d8589a52`，四板仍运行同一固件 build `20260916151903`，包 SHA-256 `3be05d080112b40cd33549322af3408549bdecd48f57b385efe99480c21d9222`；工具和测试变更未改变固件字节。A/B Release 随 P3 构建通过，全部板卡 STOP 后再次读回身份、build 和诊断，`stopped-diagnostics.json` 的 all_stopped 为 true。
- 原严格质量结果仍为 false，原始启动稳定窗口超时保留；四板 receive_missing 增长、VDC deadline 及部分从板 RefMem 超预算/隔离等均列 WARN，未改为数据零缺失或锁相通过。实际基本新帧运输、矩阵读回、原生采集/STOP 与源码指纹通过，故本轮固定范围调试验收可完成。详见 `p3-r1/alarms.json`、`diagnostic.json`、`acceptance.json` 和 `fixed-scope.json`。
- 后续工具加固记录：独审提出可进一步在凭证校验器中直接比较 diagnostic 原件的 failures 与 receipt 数组，防止手工同步重写摘要数组造成遗漏。当前生成器使用同一数组，本轮由独审直接核验三方一致；该额外加固不扩大本轮验收范围，不隐去原始质量问题。
- 下一 gate：验收工具切片完成后回到 `VDC-FAST-002`。运输/调度的严格质量问题继续按主线处理，不将本工具优化扩为锁相实现或重新开放 RAM 缩容。
- 提交记录：用户授权先提交当前改动。`e6ff06a` 包含当前固件、工具、测试和凭证；提交前 `check-staged` 及提交钩子均实际核验匹配源码/原件，非此前“无 staged 代码”的跳过检查。文档单独提交，未 push；后继最小同步记录与 Core1 接线仍在只读审计，未混入实现。

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
