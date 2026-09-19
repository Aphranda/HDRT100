# VDC task progress archive 07

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_07.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-19

### VDC-PROGRESS-20260917-029：共同边沿规划与状态机分支借鉴

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS；证据根
  `out/HardwareAcceptance/20260917/dpll-run-output-r1/`。本节数字为验证快照，
  非产品事实源。本切片实现边沿数学准备，尚无持续 RUN 硬件 consumer，
  不能据此宣称物理 delay 已生效、单圈期限满足或百纳秒锁相。
  代码提交 `ac57b8e` 已通过同指纹 pre-commit 硬件门禁；他人
  `components/tdma/src/tdma_flight_engine.c` 未暂存、未修改、未提交。
- `vdc_domain_dco_output_to_local_ns` 导入精确整数反解：正频率用 floor 几何，
  负频率按 ceil 平台取最早交点；支持负截距所需的超宽中间需求，不使用浮点、
  二分搜索或动态分配。失败保持输出，成功仍保留实际投影可能越过目标的整数余量。
  `vdc_output_edge_plan` 选择共同严格未来网格点，尊重未提交序号下限，
  独立有符号 delay 在本地物理轴只加入一次；本地 ns 尚未换算成 TIMER1 原始 tick。
- 首轮 host-r1 的 inverse/既有 phase 共 18 项通过，planner 因夹具少传 context
  编译失败；修复后 r2 的一项新边界预期多算一拍，独立 oracle 与实现一致，
  已修正测试输入并保留失败。host-planner-r3 共 5 项通过；独立方最终生产
  inverse/planner 合并 10 项通过，覆盖 30000/1800 个随机输入及极值、平台、
  空指针、非法模型和失败不变。候选/生产函数一致性与独审见
  `control-review/production-math-review.json`，纯计划测试不充当 FIFO 不变性证明。
- 新目录 Release `out/build-run-output-r1` 成功，build ID `20260917135528`；
  A/B/boot Flash 链接检查通过。源码指纹
  `8d36ff03a46a454e7c65f29e0ef0c2f7c3a76a06b1c33c30cc0d13f9ef32290e`，
  1255 文件。资源独审 `resource-review/release-binding-review.json` 验证
  ELF→BIN→PKG 绑定；静态 RAM 增量为零，主 RAM 余 32428 B、scratch 余 24 B，
  原审计范围内 Core1 最大栈仍为 2872/3072 B。inverse 对象为 832 B，但未被
  生产调用，最终已链接 GC；planner 也尚无生产调用，不能把候选 host 成本当成
  Core1 WCET。既有 forward 仍在原运行链，不将其误记为未接入。
- 同源码四板 quick P3-r1 完成，耗时 185.953 秒，`PASS_WITH_WARNINGS`，
  25 INFO、18 WARN、0 ERROR/FATAL；复用已测线序，没有重新 P0T 扫描。
  严格 TDMA、闭环及实时质量失败原件保留，DPLL 专项为 SKIPPED_TDMA_ONLY。
  本切片专项为纯数学实际 C/oracle 验证，未生成新物理输出锁相证据。
  四板采样窗均有 TX/RX 序号推进及有效新输入，坏帧增量为零；结束均已应用
  STOP，原生记录逐板保留。独立凭证/分级复核见
  `resource-review/p3-reference-review.json`。文档 C11 为
  `ACCEPT_V12_PENDING_CONTRACT_SCOPE`，见
  `control-review/c11-v12-final-independent-review.json`；登记保持 pending。
- 根据用户建议，只读审计
  `origin/feature/node-sequence-reservation-trigger`，固定 SHA
  `f4ab099eea2fa399149bff7b1861ceca94347d9b`；没有 checkout 或整分支合并。
  `branch-review/node-sequence-review.json` 与 `source-bindings.json` 保留
  文件行、git blob 和当前实现对照。可采纳四路预编码同拍写、Core1 执行交接、
  PIO 内部 READY/IRQ 握手、written/completed 分层回执及 STOP 退休顺序。
- 分支现有 executor 采用 100 ns 指令 tick，静态 DMA 无限重放，资源声明为
  31 条 PIO 指令及 3–4 SM/4–5 DMA；这些是分支代码快照，不能直接作为
  当前连续 DPLL 输出后端。其硬件触发没有建立跨板共同 VDC/TIMER1 时间锚，
  active 后 pull 与回执背压可能延长脉冲，原方案不能原样用于持续精确输出。
  全分支覆盖还会丢失当前输出改进并牵入 Flash/OTA 改动，未采纳。
- 后续方案保持统一 SYNC_IO 管理全部 IN/OUT；DPLL/VDC/SYNC 都是特等席。
  借鉴预编码和硬件执行做法，使用当前 board 时钟/divider 的指令节拍，
  不由 CPU 逐边沿调度。下一 gate：STOP 资源预留及 Core1 owner 交接，
  小型 PIO 执行器低态预取完整动作，有界有限块补给与已提交前缀保护，
  原始时间锚/迟到/断流/STOP 留证，再做同源码四板 P3 与有限四通道示波器专项。
  已有固定模型诊断输出不能代替持续模型更新；长期目标保持进行中。



### VDC-PROGRESS-20260917-028：独立输出 delay 持久化与本地相位提交

- TODO task ID：`VDC-TUNE-003` DONE、`VDC-FAST-003` IN PROGRESS、
  `VDC-OUTPUT-001` PENDING。证据根为
  `out/HardwareAcceptance/20260917/dpll-local-phase-r1/`；本节数字为实测/构建
  快照，非产品事实源。输出 delay 为每板独立有符号 ns，默认零，正延后、负提前；
  不重复加入 MATCH 已使用的链路传播 delay。实际 RUN 输出 consumer 尚未接入，
  配置持久化不等于物理边沿补偿生效，也不宣称百纳秒锁相。
  代码提交 `c4df028` 已通过真实 pre-commit 同指纹硬件门禁；他人
  `components/tdma/src/tdma_flight_engine.c` 保持未暂存、未修改、未提交。
- 新增 STOP-only `SYSTem:VDC:OUTPut:DELay <ns>`、`DELay?`、
  `DELay:DEFAult`、`DELay:RECall`、`DELay:STORe`。SET/default/recall 经
  TDMA STOP metadata guard；显式 STORE 经 Core0 既有维护与 FlashTransaction。
  Product Config v4/76 B 保留 v1/v2 的 64 B、v3 的 72 B CRC 域，旧记录启动只迁移
  RAM，补偿缺省零，保留 PI、角色、基线、USB 与板号；不修改 OTA 实现。
- 首次 `delay-hil-r1` 在任何 STORE 前发现真实目标上的通用 int32 参数转换会
  将 `2147483648` 静默截成最大值。失败及清理原件保留；现改为完整单个十进制
  token 的带符号溢出检查，拒绝小数、指数、单位、进制别名、引号及多余参数，
  所有拒绝发生于 RAM 修改前。真实 libscpi 输入与配置 harness 共 52 项通过，
  见 `delay-parser-host.xml`；不能只凭 mock 参数测试声称真实解析正确。
- 集成 trace 回归 190 项、phase 回归 147 项通过；独立 Domain 整数预言机、
  相位控制/频率共存、取消与模型收据复核保留于 `control-review/`。
  相位模式默认关闭，STOP 显式开启，新 FOLLOW 绑定锁存；同事件残差向最近零边界
  有限平移，Domain 实际修改 base_vdc 后须取得完整模型发布收据。仅已确认的自身
  相位平移允许频率估计坐标归一化；频率改模、未知模型及 STOP 取消退休基线。
  schema4 原生记录区分 MATCH、频率 DECISION 与实际 PHASE；负向时间坐标平移
  仅用于调试捕获，不授予单调 RUN/GPIO 输出。公共 FOLLOW ABI 保持兼容。
- 最终 Release build `20260917124808`，源码指纹
  `e4aad13282ae46ab801b5d1104044b28343876afe2ed48172978554c2052ed26`，
  1252 文件。增量构建复用 build ID，必须结合源码/package 哈希辨识；双槽
  ELF→BIN→package 精确绑定见 `resource-review-strict/release-binding-review.json`。
  相对进度 027 主 RAM 增用 588 B，余 32428 B，scratch 余 24 B，已审 Core1
  最大栈仍为 2872/3072 B。严格 parser 后续修复仅 Core0 代码变化，RAM 与
  Core1 可达调用边均无增量；没有将 Flash、SCPI 或 RTOS 加入实时处理。
- P3-r1 在 ARM 前 NO3 topology 配置超时/Execution error，原件保留，不推定
  物理链路或相位算法故障；P3-r2 通过但因 parser 后续修改而不再是当前凭证。
  最终同源码 `p3-r3` 用时 180.547 秒，`PASS_WITH_WARNINGS`，25 INFO、19 WARN、
  0 ERROR/FATAL。严格 TDMA、闭环及实时门禁原始失败继续保留；DPLL 未列为该轮
  P3 必验，不将快速流程通过提升为严格质量或锁相通过。
- `delay-hil-r2` 实板通过，工具耗时 19.797 秒，终端包络 22.985 秒。
  四板核验 UID/build/STOP/初值，仅 NO2 显式保存 125 ns，用不同 RAM 哨兵再重启
  证明 Flash 回读；随后保存原持久值、再次哨兵/重启，最终恢复原 RAM 请求。
  两次 STORE、两次 RESET；合法正负边界、非法值不变、默认与召回分别验证，
  PI/角色配置/基线/身份不变，RAM/Flash 无遗留恢复项；NO2 重启后的角色
  applied generation/pending 状态改变，不能声称完整角色状态元组不变。
  独审逐条复算 234 条指令通过，见 `parser-review/delay-hil-r2-independent-review.json`。
  实机原 RAM/Flash 值均为零；两者不同时的恢复由 host 覆盖。实机持久化目标限于 NO2，
  不写成四板分别完成保存/重启专项。详见 `delay-hil-r2/report.json` 与命令原件。
- 首次相位专项 `native-phase-r1` 在 START 前发现 NO2 重启后的 TRN03STG 为
  EMPTY；未 ARM/运行，相位执行尚未发生。失败保留，后续使用本轮 P3 的同一
  矩阵重新装载并逐板读回，不重新扫描线序、不校准、不追加 Flash 保存。
- `stage-restore-r1` 装载/读回通过，但该工具将 forwarding 切至 raw-flight；
  后续 r2/r3 在 NO4 ARM 以 adapter 278（PHYS geometry）拒绝。STOP 后恢复
  process-image 模式，r4 已运行并保留 NO3/NO4 各 55 个 MATCH，但 FOLLOW 在
  首 ticket 执行前以 BINDING 取消，没有 PHASE。NO2 还缺少重启前的 VDC
  provisional 绑定与 debug admission。原件及独审完整保留，不将相位调用前的
  取消误报为 delta 算法拒绝；BINDING 的精确子条件未单独留证，不指定猜测根因。
- 随后恢复 NO2 VDC 配置，并用既有短 TDMA 工具恢复四板的完整 P3 运行上下文，
  包括 clock evidence、provisional 和 debug admission。`tdma-context-restore-r1`
  四板 ARM 成功，但因原 TDMA recorder 仍保留旧记录而在新 recorder ARM 拒绝，
  未 START，已 STOP；这一轮不记为通过，也未清掉旧记录。无需修改生产源码。
- 恢复完整上下文后 `native-phase-r5` 通过，终端耗时 35.781 秒，静默运行约
  11 秒、临时许可 20 秒，运行零查询，全部 STOP 后导出。NO2/NO3/NO4 实际确认
  相位提交分别为 109/6/36 次，均 committed=applied，拒绝零；频率实际调整
  2/0/8 次，不调整 9/8/5 次，末态 −46/0/+1805 ppb，末态 DCO 序号 112/7/45。
  这些是控制值，不是外部残余频差或锁相精度。NO2 刚重启，原生首相位残差约
  −109 秒，以有界步幅逐次追赶；不能把时间原点差当成晶振频差。
- schema4 原件只保留有限前缀：三从 MATCH/DECISION/PHASE 数量分别为
  24/4/48、55/8/6、39/8/29；满池后的过程不可由末态补造。同一次运行中
  相位提交后继续有频率决定，STOP 最近相位尝试（包括跨零 held）与最近频率
  决定共同核对真实 Domain 模型；不宣称完整频率归一化链或物理输出已验证。
  四板原生 binary 重解码及控制量独审见 `control-review/native-r5-independent-review.json`；
  590 条操作、零 RUN 查询、有限许可、STOP 后导出及清理独审见
  `parser-review/native-r5-lifecycle-review.json`。契约 C11 结论为
  `ACCEPT_V11_PENDING_CONTRACT_SCOPE`，登记保持 pending，原件见
  `control-review/c11-v11-final-independent-review.json`。
  NO3/NO4 最新内部残差区间仍为数微秒宽，NO2 尚在追赶，不能标为 LOCKED。
- `final-stopped-state.json` 独立确认四板 STOP、会话零、输出 PIO/DMA idle，
  示波器 STOP/EXT/NORM。下一切片为 `VDC-OUTPUT-001`：确定性输出 owner
  在启动锁存独立 delay，采用共同未来 VDC 边沿，记录模型/生效边沿，再以 NO1
  相对三从的实际上升沿差验证相位、斜率与抖动；长期目标继续 IN PROGRESS。

### VDC-PROGRESS-20260917-027：基线 SCPI 配置与显式 Flash 保存

- TODO task ID：`VDC-TUNE-001` DONE、`VDC-FAST-003` IN PROGRESS。
  证据根为 `out/HardwareAcceptance/20260917/dpll-scpi-baseline-r1/`，本节数字
  为本轮快照，非产品事实源。用户选择先开放早期基线替换次数和窗口，
  本轮不改变频差估计、响应比例、死区、评估档位或 NO1 PI。
  代码提交为 `8b12944`，包含匹配的 P3 凭证，真实 pre-commit 按当前 staged
  指纹通过；他人 `tdma_flight_engine.c` 修改未暂存、未提交。
- 已实现 STOP-only `SYSTem:VDC:PRIORity:FOLLow:BASEline <次数>,<窗口ns>`、
  `BASEline?`、`BASEline:DEFAult`、`BASEline:RECall`、`BASEline:STORe`。
  工厂值为 2 次/250000000 ns；次数 0 关闭质量替换，窗口为正且不超过工厂上界。
  Core0 发布整对原子配置，新的有效 FOLLOW 绑定一次锁存；旧 pending、模型更新
  和基线重建不混入新参数。STOP 退休请求，后续须重新显式请求 FOLLOW。
- Product Config 增量迁移为版本 3/72 B，保持旧 64 B 前缀与旧 CRC 域。
  新固件只读启动迁移保留有效旧 PI/角色/USB/板号；显式保存走既有 journal 和
  Core0 FlashTransaction。新维护入口持有 TDMA control guard 排斥 ARM/config，
  不把 Flash 放入 Core1 或有界 metadata callback。该迁移不承诺旧固件识别新记录。
- 软件回归分别为控制/时钟/配置首轮 153 项、最终配置专项 40 项、集成 163 项；
  数量有重叠，不求和。真实 Product Config C 测试和独立迁移/写失败/CRC/轮转
  复核通过。控制专项一次测试缺少 Core1 上下文失败保留，修复仅补测试上下文，
  生产实现未为测试放宽。原件见 `control-candidate/`、`control-review/`、`review/`。
- Release build `20260917114329`，源码指纹
  `1b7bd8e8925d94d9e8d3dfe72d6528a90e9890e30072ebccde70c3f8838a7379`，
  共 1241 文件。双槽 ELF/BIN/package 逐字绑定独审通过，package SHA 为
  `12c4f97aaf49d5979f1685956d0e77502ed508febe2392064b0d1e090ed62f42`。
  新静态对象共增 20 B，其中配置字使用原有对齐空隙，最终 RAM 余量减少 16 B
  至 33016 B；scratch 余 24 B。已审 Core1 最大栈保持 2872/3072 B。
  新 getter 自身零栈、一次原子读取，但位于 XIP，不宣称已证明 RAM 常时延。
- 匹配源码四板 quick P3 用时约 191.343 秒，`PASS_WITH_WARNINGS`：25 INFO、
  18 WARN、0 ERROR/FATAL。独审重算 29 份引用、实际四板升级、每板 14 条 TDMA
  原生记录及终态 STOP；较前基线无新增 WARN。严格 TDMA/实时门禁仍失败，
  `strict_gates_passed=false`；本切片不把快速流程通过写成严格质量或锁相通过。
- 四板实读新 build、UID、STOP 和默认基线配置后，复用既有 11 秒静默原生
  采集器，运行零查询、全部 STOP 后导出，流程约 32.718 秒通过。
  三从实际 DCO 更新 5/6/8 次，末态分别 +81/+302/+2727 ppb；这些是本地
  控制值，不是相对 NO1 的外部残余频差，也不据此推断精度变差或锁相。
- STOP-only HIL 工具完成 33 项离线测试和独审后执行，238 条原始命令完整保留，
  流程终态通过，用时约 32.61 秒（工具内部 31.5 秒）。四板核身份、build、
  STOP 和初值，NO2 实测合法边界及 7 组非法输入拒绝/请求值不变，区分工厂默认
  与召回；显式保存 1 次/125000000 ns 后，先写不同 RAM 哨兵再软件重启，
  读回保存值。随后恢复原持久化 2 次/250000000 ns，再次哨兵/重启验证，
  最终恢复原请求。PI 前六字段、角色前两字段与身份不变，代际字段原样留证，
  不要求跨重启相同。两次重启、清理均成功，无遗留 RAM/Flash 恢复项。
  主要耗时来自非法命令等待响应；不属于实时环路成本。
  实机保存/重启目标仅为 NO2；维护/ARM 竞争和新绑定锁存由真实 C host 覆盖，
  本轮原生回归采用默认参数，不声称非默认运行轨迹已全部硬件验证。
- 最终四板会话为空、PIO/DMA idle，示波器 STOP/EXT/NORM 实读通过。
  原件入口为 `scpi-hil/run-r1/report.json`、`commands.jsonl`、
  `review/p3-hardware-review.json`、`review/native-independent-review.json`、
  `review/origin-join-independent-review.json` 和 `final-stopped-state.json`。
  HIL 工具初审发现重连沿用旧身份验证集合，已修复并以错 build 重连负测验证，
  此问题在操作硬件之前闭合，初轮离线产物保留。
  独立 HIL 审核逐条重放全部命令，确认恰好两次 STORE/两次 RESET、非法输入
  错误队列及状态恢复，见 `control-review/run-r1-scpi-hil-independent-review.json`。
  `VDC-PRIORITY-01` v10 C11 结论为 `ACCEPT_V10_PENDING_CONTRACT_SCOPE`，
  登记状态仍为 pending，不由参数保存切片升级为已锁相。
- 用户明确 ±50 ppb 是后续频差优化目标，以当前频差直接推进实际输出 100 ns
  量级锁相，因此本配置切片不再追加无关的 STOP 固定频率波形门禁。
  下一 gate 为复用同事件 MATCH 残差，接通 Core1 本地相位校正、
  Domain 提交及 RUN 输出确定边界采用，记录事件/模型/生效边沿。
  频差优化独立记为 `VDC-FREQ-001`，不阻塞相位闭环；ACK、误差预算和一致 VDC
  发布继续分别验收，不把当前配置切片完成提升为长期目标完成。
  `phase-next-plan.json` 为只读接续方案：特别覆盖频繁相位改模不能饿死现有长窗
  频率估计，以及现有纳秒命名输出接口仍用微秒启动坐标，不能仅移除 STOP 门禁
  或凭接口名字声明百纳秒相位。先形成真实相位修正证据，再收紧完整误差预算。

### VDC-PROGRESS-20260917-026：单次启动连续观察与独立 STOP 末态

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-continuous-settling-r1/`，数字为本轮
  快照，非产品事实源。固件保持代码 `94c77a8`、build `20260917105000`、
  源码指纹 `9417181385bd49e04917e6494f61ba9503dec6caca9084636dcfc3c3b099223e`。
  重新核对匹配凭证与实际四板身份，未重刷、重测 P0 或改变控制算法。
- 采集器经过 51 项离线正负测试和独立复核，单次 START 后静默约 18 秒，
  临时 TRIAL 为 20 秒。TRIAL 发送前至 NO1 STOP ACK 包络约 18.188 秒，
  全板停止读回包络约 18.469 秒，余约 1.531 秒；该包络依赖工程时钟假设，
  不声明物理时钟标定。运行零查询，全部 STOP 后导出并验证 CRC/ACK/代际。
- 三从各保留 76 条有限原生前缀，均按真实 follower FULL 语义冻结：首次
  被拒绝追加使 dropped 为 1，不把该事件造进原件，也不称全窗已记录。
  共 198 MATCH、30 DECISION 的数学与控制量独立复算通过。每板前缀之后
  另有 4 个完成结果，仅用 STOP 计数证明数量差；未知中间过程不拼接。
  STOP 末次决定、局部投影、真实 DCO 及绑定另行核验。
- 全窗 NO2 无实际调频、13 次不调，末值 −1166 ppb；NO3 调频 2 次，
  −941→−909 ppb；NO4 调频 3 次，1475→1558 ppb。NO2 首组约 8 秒档
  为 [−156,+3] ppb，因跨零未调；NO3 相应档为 [−176,−17] ppb，实际 +8 ppb。
  这些决定符合现有保守控制律，不说明物理零误差。
- NO1 从 START 前 −1072 ppb 调到首成功编码时的 −1162 ppb，此后至 STOP
  committed token 115 不变；对本轮有效跟随窗口证明没有新的已发布模型。
  该证明不等于晶振不漂移，也不把启动前变化归入有效窗口。
- 新鲜固定输出的 40 个波形块、20 组阈值/通道组合与真实末态绑定独审通过。
  相对 NO1 的条件频差约为 NO2 [−146,−53]、NO3 [−149,−73]、
  NO4 [−172,−82] ppb；仍偏慢，未达锁相或 100 ns 相位资格。
  四板会话已清空、PIO/DMA idle，示波器 STOP/EXT/NORM 读回通过。
- 原件入口为 `source-binding.json`、`review/hardware-stage-review.json`、
  `review/native-independent-review.json`、`physical-review/physical-review.json`、
  `scope-fixed-r1-all-edge.json` 和 `final-stopped-state.json`。
  原始有限前缀与独立末态的范围分别保存，不以 scope 成功追认完整过程。
- 下一 gate：按用户新增要求先将早期基线次数/窗口接入 STOP-only SCPI 和
  显式 Flash 持久化，以缩短后续迭代；保持 HAOFV Core0 写 Flash/Core1 固定
  快照边界，兼容旧 PI/角色配置，每次功能变化仍跑匹配源码四板 P3。
  现有 PI 等参数的 SCPI/保存能力与未开放项分别盘点，不能宣称全参数已实现。
  用户随后明确频率稳定度不应阻塞相位闭环：完成参数入口后优先接通同事件
  残差→本地相位提交→RUN 物理输出，不以频差接近零为前置。只读路径审核见
  `out/HardwareAcceptance/20260917/dpll-scpi-baseline-r1/control-review/phase-path-gap-review.json`。
  当前 rate 更新保持相位连续，既有 fixed-output 请求不携带公共 VDC 起点，
  因此本轮 STOP 固定模型波形不证明绝对相位或运行态锁相。MATCH 模型坐标偏移
  也不能直接解释成实际 GPIO 偏差；时间戳、delay、输出量化和有效校正间隔须
  在基础相位闭环接通后共同预算，ACK 与一致 VDC 发布仍待完成。
  用户追加确认将 ±50 ppb 留作后续频差目标，以当前频差水平直接推进实际输出
  100 ns 量级锁相；两者为任务目标而非测量结论，不新增频差达标的前置门禁。

### VDC-PROGRESS-20260917-025：有界窄基线采用及示波器反馈纠偏

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-baseline-quality-r1/`；本节数字均为
  本轮快照，非产品事实源或物理精度契约。代码提交 `94c77a8`。
- 首档消耗前，同模型与完整绑定下允许最多两次早期基线替换，每次新源区间
  至少减半、完整远端间隔上界不超过 250 ms；实现符号见域架构。最多增加
  500 ms 参考坐标预算，不是缺帧时的墙钟保证。使用新事件自己的真实端点，
  没有候选即保留原基线，不新增质量前置或退还已消费档位。正常重建、取消、
  STOP 和实际应用清理私有次数，响应比例及五档估计保持。
- 控制/窗口回归 115 项、映射/投影/trace 集成回归 158 项通过，两组覆盖存在
  交集，不累加。Release build `20260917105000`、源码指纹
  `9417181385bd49e04917e6494f61ba9503dec6caca9084636dcfc3c3b099223e`
  通过双槽资源及源码独审。私有状态增加 8 B，公共快照保持 216 B；主 RAM
  扣堆余量 33032 B、scratch 24 B。prepare 局部栈增加 16 B，当前 persona
  Core1 最大链仍为 2872/3072 B，无新增可达函数/调用边，不代替 WCET。
- 同源码四板 quick P3 用时约 229 秒，23 INFO/22 WARN、零 ERROR/FATAL。
  相对上一轮新增 T1/T3 各两项告警：有效训练样本未覆盖足够候选、SCK 行的
  最佳 rearm margin 为 −1 sample。实际运输通过，严格质量仍失败；不把
  本轮质量差异归因于基线策略。源码/包/双槽 ELF/OTA/板端原件及暂存指纹
  均复核，`p3-warning-comparison-map-retention.json` 保留告警对账。
- 第一段静默原生采集：165 MATCH、38 DECISION，三从实际应用 8/8/9 次，
  末 DCO −1451/−1232/+626 ppb。三从首 MATCH 的源事件均为 61、宽 1687 ns；
  首 DECISION 实际基线均为后续事件 65、源宽 567 ns，相隔约 4.001 ms。
  证明真正使用了更窄的后续基线；稀疏记录不证明中间确切替换次数。没有
  完整源端 DECISION 端点对，不凭空补出遗漏坐标。NO1 首成功编码至 STOP
  的 committed token 38 不变，末参考为 −1906 ppb。
- 第一段新鲜四通道固定输出波形独审通过，40 块原始数据、整数输出计划、
  原生末态模型与 2048 脉冲完成状态对应。相对 NO1 的条件频差约为
  NO2 [+211,+307]、NO3 [+217,+322]、NO4 [−367,−273] ppb；仍有漂移。
- 保持同一固件和从板已采用 DCO，追加一段有限静默窗口：165 MATCH、
  39 DECISION，三从各实际应用 8 次，末 DCO −1166/−941/+1475 ppb。
  NO2/NO3 的首样本已宽 683 ns，首决策沿用；NO4 从 1687 ns 改用后续
  683 ns 基线。NO1 在首成功编码前由 −1906 调到 −1072 ppb，此后至 STOP
  token 78 不变；不能将两段看成同参考连续延长或受控 A/B。
- 第二段新鲜波形 `scope-fixed-r2b` 独立复核与 all-edge 重算通过，三从
  条件频差分别约为 [−315,−211]/[−283,−198]/[−352,−273] ppb。
  NO2/NO3 最后约 1.5 秒档估计 [−546,−55]/[−552,−61] ppb，实际增频
  +27/+30 ppb，与外部方向一致。NO4 最后新基线仅约一秒档，远端差分宽
  774 ns、估计 [−673,+102] ppb，暂不调不等于零误差或永不调整。
- 第二段首次 scope 失败保留在 `scope-fixed-r2/`：NO3 STOP 后诊断的
  `model_unchanged=2` 表示快照争用，随后安全 STOP 快照返回 1；未导出波形。
  原脚本与原判据在新目录重采通过，未修改固件、放宽判据或覆盖失败。
  所有运行窗口零查询，全部 STOP 后读取；最终四板会话清空、PIO/DMA idle，
  示波器 STOP/EXT/NORM 已读回 `final-stopped-state-r2.json`。
- 下一 gate：继续用外部频差约束内部估计与实际响应，优先在同一稳定参考的
  连续自主窗口内观察末次调整后的后档收敛，避免反复 START 引入 NO1 参考
  变化而误判方向或振荡；延长观测时预先安排有限记录容量及静默期限。
  再据原件判断是否需要进一步收窄末端区间或修改控制响应，不能以内部跨零
  代替实际零漂移。当前只关闭有界基线策略切片，ACK、实时模型输出、
  100 ns 相位精度及一致 VDC 发布仍未完成。
