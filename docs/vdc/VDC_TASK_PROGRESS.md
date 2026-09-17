# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-18

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

### VDC-PROGRESS-20260918-010：同模型有效起点变化的基线失效修复

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。修复
  `components/vdc_dpll_manager/src/vdc_priority_follow.inc`：当已保留基线的模型
  身份未改变、但发布的 `valid_from_raw` 被改写时，Core1 现在先废弃旧基线并等待
  新基线；已确认的 phase successor 仍按独立 `rate_epoch` 路径保留，不受该检查误伤。
  这样可避免同一模型 token 下把有效期前后的两个事件拼成一次频率差分。
- 主机验证：`test_vdc_priority_delta.py`、`test_vdc_priority_phase.py`、
  `test_vdc_priority_follow.py`、`test_vdc_priority_follow_config.py` 共 175 项通过；
  私有工作区大小与公共快照 ABI 保持不变。测试仅证明边界和所有权语义，不证明板端
  连续输出或物理锁相。
- Release 资源验证：构建目录
  `out/build/dpll-validity-fix/` 完成 `pico2-release` 编译、双应用镜像和链接检查。
  当前源码四板 quick P3 使用 `--tdma-only` 完成，证据根为
  `out/HardwareAcceptance/20260918/p3-055020/`，结果为
  `PASS_WITH_WARNINGS`（30 INFO、24 WARN、无 ERROR/FATAL）；默认五板配置因 NO5
  未连接而停止，四板范围已显式固定，DPLL 外部观测未纳入本轮判定。
- 提交：`80435e65 fix(vdc): retire baseline on same-model validity change`，P3 凭证
  已绑定提交源码指纹。下一 gate 仍是 `VDC-FAST-003`：在不改变特等席运输边界的前提
  下，复采 NO1--NO4 的实际接收/采用事件，再统一到同一时间轴核对补给连续性、DCO
  生效和 internal 残差；不能用本次模型边界修复替代连续输出或 100 ns 锁相证据。

### VDC-PROGRESS-20260918-009：长时间轴补给窗口 A/B 复采

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。以下均为四板有限
  20 秒调试快照，数字不是 WCET 或产品契约；证据根为
  `out/HardwareAcceptance/20260918/dpll-run-timeline-r1/timing-longaxis-r7/`。
- 修正采集器后，候选 `OUTPut:TIMing=(24000,32000,16000)` 在四板 STOP 状态下
  锁存成功，1 ms 输出对应 16 边沿块；schema8 RUN/native 采集通过，采集期间查询
  为零，最终四板均由显式 STOP 以 `CANCELLED` 收尾。配置试验未写入 Flash，外层
  harness 已将四板 RAM 值恢复为原请求。
- 与同固件的默认 `(12000,16000,6000)` 复采对照，NO1–NO4 的有限运行由全部
  `STARVED` 变为全部 `CANCELLED`。末次服务对应的条件 runway 由约
  `-0.47/-0.81/-1.32/-2.13 ms` 变为 `+23.59/+20.35/+20.11/+18.96 ms`；
  服务最大间隔仍约 `3.90/4.70/4.88/4.25 ms`。这说明扩大规划/承诺时间轴和
  16 边沿块在本轮显著提高了补给余量，不能外推为长期连续性或锁相完成。
- NO4 曾出现一次 `dma_not_ready`，但最终缓存计划仍完成提交；该字段和聚合计数
  没有逐次时间戳，仍需后续相关 trace 才能区分 DMA 退休、模型失效和服务调度的
  因果关系。示波器未用于本轮精度判定，DPLL/100 ns 结论保持未宣称。
- 下一 gate：在保持候选窗口可配置、STOP/代际取消和前缀不可改写的前提下，重复
  长时间轴 A/B，并将服务间隔、提交间隔、FIFO 退休和物理边沿统一到同一时间轴；
  只有连续输出证据闭合后，才进入 DPLL 锁相及 VDC 发布判定。

### VDC-PROGRESS-20260918-008：schema8 RUN 输出采样与断流因果复核

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。schema8 适配的 RUN
  输出工具只写入 `out/`，未进入产品源码；本条数据和数值均为一次调试快照，非
  WCET、连续性或精度契约。
- 新 build `20260917204319` 的四板有限 RUN 采样已完成，证据根为
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/capture-r1/`，采集期间查询数
  为零，STOP 后统一导出 native 与示波器数据。四板 RUN schema 均为 8；NO1 提交
  1140 个块后由采集清理得到 `CANCELLED`，NO2/NO3/NO4 分别提交 92/57/107 个块，
  最终均为 `STARVED`。四板 `last_submit_failure` 均为 `NONE`，本轮没有观察到新的
  guard 或 NOT_READY 提交拒绝，因此不能宣称后缀重试已经在实板上被触发。
- STOP 原生差分分析位于
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/refill-retirement-analysis.json`。
  NO2–NO4 仍同时出现 TX FIFO 空、PIO stall、DMA 剩余为零；最后服务到末沿界的
  负 runway 约为 0.23/0.83/1.28 ms，服务最大间隔约为 3.59/4.74/3.15 ms（快照）。
  这继续指向 Core1 服务空窗导致的末端耗尽；没有证据表明 `NOT_READY` 缓存保留能
  消除该问题。NO1 的末段由显式 STOP 取消，不能与从板 STARVED 混为一谈。
- scope 采样成功导出，当前专项只确认数据留存和退休寄存器因果；没有进行四路同序
  100 ns 判定，也没有运行 DPLL 锁相专项。下一 gate 是在保留 `GUARD` 清除、
  `NOT_READY` 有界复用的规则下，继续优化 Core1 服务调度/提交提前量并进行受控
  A/B；不得以 `PASS_WITH_WARNINGS` 或后缀缓存命中数替代连续输出证据。

### VDC-PROGRESS-20260918-007：提交拒绝分类与私有后缀重试边界

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。本条记录当前源码
  的 host 验证；尚未形成新的板端或 Release 证据，数字均为快照，非产品契约。
- `SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_*` 将后端提交拒绝拆为参数、原始时钟读取、
  guard、编码、时钟、启动、deadline 和 DMA 等分支；`VDC_RUN_OUTPUT_SCHEMA` 更新
  后，STOP-only `RUN?` 额外导出 `last_submit_failure`。该字段只说明最近一次拒绝
  分支，不能把一次拒绝改写成连续输出或锁相通过。
- 私有未提交后缀的处理边界已固定：只有 `NOT_READY` 保留完整后缀，下一次 Core1
  服务必须重新通过 generation、代际、前驱尾点、时钟和 DMA 准入后才能重试；已提交
  前缀不重写。`GUARD` 表示硬件剩余运行道已进入最小保护窗口，和 RAW、CLOCK、
  ENCODING、START、DEADLINE、DMA、ARGUMENT 一样清除后缀，不能用重试掩盖断流。
- `tests/python/test_vdc_run_output.py` 的生产客户端与 parser 回归共 149 项通过；
  覆盖 guard 拒绝后清除、NOT_READY 拒绝后复用同一缓存、模型/前驱变化失效及取消
  生命周期。该 host 结果不证明板端重试安全，也不放宽 `STARVED` 判据。
- Release build `20260917204319` 的 A/B/Boot 双槽链接检查通过；同源码四板
  `--tdma-only --diagnostic-continue` quick P3 已完成，证据根为
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/p3/`，结果为
  `PASS_WITH_WARNINGS`（23 INFO、22 WARN、0 ERROR/FATAL）。P0 复用拓扑的身份和
  build 读回通过；T1/T3 训练质量与 TDMA 严格闭环失败原件保留，不能视为连续输出
  或锁相通过。该 P3 切片没有执行 RUN 输出专项，因此不把它解释为 schema8 实板重试
  已验证。
- 工作区仍保留另一设备对 `components/tdma/src/tdma_flight_engine.c` 的外部修改，
  未修改、未暂存。当前 schema8 代码、测试和新 receipt 尚未提交；下一 gate 是用同
  源码进行 schema8 RUN 输出采样，重点核对 `last_submit_failure`、后缀复用/清除与
  FIFO 退休时序。若实板仍在末沿后才服务，继续调整调度/准入时序，不能仅依赖缓存
  保留结案。

### VDC-PROGRESS-20260918-006：服务序号与退休点相关采样

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。本条仅记录本轮
  schema7 诊断快照，数值不是产品契约；原始证据根为
  `out/HardwareAcceptance/20260918/dpll-run-uniform-r3/capture-r2/`。
- 为每次有效 Core1 服务增加 `service_sequence`，并把最后一次客户端 outcome、
  缓存失效分别绑定到服务序号和 backend service tick；STOP 导出字段总数由 schema6
  的旧布局扩展为 schema7。主机 RUN 解析回归与时间轴回归共 170 项通过。
- 四板采样期间查询数为零，最终均为 `reason=STARVED`，且退休寄存器同时显示 TX FIFO
  空、PIO TXSTALL 置位、DMA remaining 为零；因此本轮确认的是输入耗尽事实，不把
  `PASS_WITH_WARNINGS` 或有限采样当作连续输出通过。四板最后服务到退休约 0.43 µs，
  末次服务已落在最后下降沿完整时间界之后约 33 µs 至 1.30 ms。
- NO1 的最后客户端结果为 `submit_rejected`，发生在服务序号 756；前一次缓存失效为
  序号 754，之后没有形成第三个十边沿块。NO2–NO4 的最后 outcome 分别落在序号
  1232/1435/1179，缓存失效序号为 1239/1421/1185；这些序号只建立先后关系，不能
  单独证明 DMA 不可准入、模型重建或 deadline 哪一个是根因。
- 本轮最小结论是“短服务体量已足够小，但服务空窗和提交竞态仍可把补给推迟到末沿之后”。
  下一切片应在保持 schema7 相关字段的前提下，细分 `submit_rejected` 的内部拒绝原因，
  或先验证提交失败保留私有后缀的重试策略；不得通过放宽 STARVED 判据宣称连续性。

### VDC-PROGRESS-20260918-005：固定脉宽单字 FIFO 后端（待四板专项）

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。以下为实现与离线
  验证快照，非产品事实源；新证据根为
  `out/HardwareAcceptance/20260918/dpll-run-uniform-r1/`。
- RUN 在 STOP/PREPARE 计算固定高段 tick，并调用
  `sync_io_run_output_prepare_uniform`。PIO 状态机初始化时把高段计数置入 ISR，
  每个脉冲只从 TX FIFO 取低段计数；原双字可变脉宽 API 保留。首块仍由 CPU 预装
  一个字，其余 DMA 按实际字数提交；模式切换、末项宽度、STOP/失败清理由统一
  owner 处理，已提交前缀不可改写。
- 主联合回归 748 项及另行时间轴回归 22 项通过；联合覆盖真实 pioasm、编码边界、
  backend 故障注入和有限 FIFO 模型。理想模型的 DMA 退休后库存快照
  由双字约 5 ms 增至单字约 9 ms；该数字是模型快照，非目标板 WCET 或连续性证明。
- 新 Release build `20260917190453` 双槽链接通过；资源审计显示主 RAM 扣 heap
  后约 22348 B、scratch 约 24 B、uniform 增量约 32 B，完整 RUN 新栈上界约
  1536 B，仍有四个 SDK XIP 叶子。上述均为本构建快照，事实以链接和审计原件为准。
- 文档契约 `VDC-PRIORITY-01` 更新至 v15，仍 pending；独立 C11 结论为
  `ACCEPT_V15_PENDING_SEMANTICS_NO_HIL_OR_PRODUCT_RELEASE_CLAIM`，原件在
  `design-review/c11-v15-semantic-independent-r1.json`。同源码四板 quick P3
  PASS_WITH_WARNINGS，181.917 秒，25 INFO/18 WARN/0 ERROR/FATAL；严格质量
  失败保留。实际 scope 复采尚未完成，不能据 host/P3 通过宣称连续或 DPLL 锁相。
- uniform 四板实际采集 `capture-r2` 已完整 STOP/导出 scope 与 native：schema 6
  的 `fifo_words_per_edge=1` 四板均生效，但 NO1–NO4 仍因 FIFO 耗尽 STARVED。
  服务末间隔约 2.17–2.65 ms，退休时末沿界已落后约 0.33–2.30 ms；短服务
  wall 约 28.7–46.7 µs，超预算计数为零。以上为本轮有限采集快照，不能外推
  WCET 或锁相；最终 STOP 读回和示波器 STOP/EXT/NORM 均通过，原件在
  `capture-r2/`。
- 独立专项复核 `design-review/uniform-capture-independent-r2.json`（SHA256
  `800d2630a645e8cebe2850f5ca905dcafdb753aaeeb5d760e834212d202e17c4`）确认四板
  实际 uniform、高段 500 tick、采集期零查询及 STOP 收尾；将 harness 因检测到
  STARVED 而返回非零视为专项 FAIL 原件保留，不改写为通过。
- 该结果表明单字编码增加了 FIFO 库存密度，却未跨过 Core1 补给调度空窗。
  下一 gate 转为对齐调度服务间隔、模型后缀准入和 DMA 退休期限的相关时序证据，
  再决定是否调整 TDMA 服务预算；不通过放宽连续性判据结案。

当前执行方向由用户进一步明确为预编码最小时间戳、确定性特等席运输和 Core1
直接匹配，见 `VDC-PROGRESS-20260916-038/039/040/041/042/043` 与 `VDC-FAST-001/002/003`。
此前“先核对或替换 OSAL 发布时基”的下一步由该方向覆盖；已通过的分片运输和
本地采用证据保留，不提升为逐圈确定性或锁相完成。

提交边界：`e6ff06a` 已提交 priority RX/栈与调度收敛、RefMem 缩容、验收分级及匹配的
四板 quick P3 凭证；暂存源码指纹 `fb2b46871987abe1426a52207966c93942b75fea1a58a53adb396be2d8589a52`
已由真实 pre-commit 硬件门禁核验，见 `VDC-PROGRESS-20260917-001`。严格质量告警仍保留，
不授予单圈同步编码、三从闭环或锁相完成。

### VDC-PROGRESS-20260918-004：有限长时间轴与可配置补给窗口

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  本条数字为证据快照，非产品事实源；证据根
  `out/HardwareAcceptance/20260918/dpll-run-timeline-r1/`。
- 同次有限 RUN 复用初始 raw/local bridge，后续绝对目标映射到同一硬件时间轴；
  规划和准入仍读取新鲜 raw 时间，不累积舍入周期，不修改 NO1 PI。较长未提交
  后缀每次最多规划四个边沿；模型 token 或前驱改变时丢弃部分/完整缓存。
  backend 支持有界实际边沿数，先全量校验再编码；首个单边沿块不启动零长 DMA。
- `OUTPut:TIMing` 提供规划/承诺/低水位完整三元组；STOP 且客户端空闲时设置，
  PREPARE 锁存并联合检查周期/容量/静态表。默认请求 12000/16000/6000 µs，
  本轮 1 ms 输出对应十边沿块，最多十六边沿；默认不是已证明最优配置。
  Product Config v5 保留 v1–v4 CRC 域及非零输出 delay，仅显式 STORe 写 Flash。
- 主联合 host 671 项及独立时间轴 22 项通过，后者覆盖 20 秒运行/低字回绕、
  分批规划、库存水位、模型变化、STOP 与容量拒绝。文档检查器测试 38 项通过。
  双槽 Release build `20260917175835`，源码
  `feb9aac77b4b41978730b409d95a80dc84b20a46324d3a582bfb0ac0e9249596`，包
  `e06ee9f9df635c043d80bdb83c5a037d4163aca6922f30becce5388950cb91f6`。
  新增 Python 测试在构建过程中落地但不参与固件编译；最终源码指纹在 P3 前冻结。
  双槽 ELF/BIN/包一致；扣 heap 后主 RAM 22380 B，完整 RUN 栈 1524 B，
  cached 栈 1256 B，继承路径最大 2872/3072 B，scratch 余 24 B。
  实际 cached 调用图仍含 schedule getter、clock_get_hz、memcpy 和 GPIO helper
  四个 XIP 叶子；不沿用前轮耗时作为本轮 WCET，详见 `design-review/`。
- 同源码四板 quick P3 PASS_WITH_WARNINGS，182.546 秒，25 INFO/18 WARN，
  0 ERROR/FATAL；strict/closed-loop/realtime 质量失败保留，不当作输出专项通过。
- 配置专项首轮因工具错误地把 SET 三元组当成 OK 超时，原件保留于
  `config-hil/live-r1/`；无 Flash 写入，四板 STOP 和原请求值已读回。修正工具后
  `live-r2` PASS，37.531 秒：NO2 测试参数保存/重启/召回、原持久值恢复后再次
  重启及原 RAM 值恢复完成，四板 delay/PI/角色/基线/身份保持；RAM/Flash 无待恢复项。
  两次 RESET 的串口 ClearCommError/PermissionError 原件保留，随后重新枚举、
  身份和不同于 RAM sentinel 的参数读回闭合，不称重启全程无异常。
  新负测直接编译真实 callback/libscpi，裸 OK 不能冒充 SET 成功。
- 首次输出复采 `capture-r1` 在 START 前因 NO2 重启清除临时训练矩阵而失败；
  `TRN03STG` 返回 EMPTY，未形成有效波形，四板 STOP 留证。后续重新装载同次
  P3 已测矩阵并核对读回，`restore-stage-r1` PASS；不重新扫描 P0 或训练延迟。
- `capture-r2` 在 NO4 ARM 被 adapter 278 拒绝，未进入时间轴规划/输出；装载工具
  切换至 raw-flight。后续 STOP 恢复 process-image、clock evidence、provisional
  与 debug admission；`restore-context-r2` 全部读回通过。首次恢复脚本把
  provisional 回读误按六字段解析（实际七字段），失败与四板 STOP 留于
  `restore-context-r1`，未改固件。
- `capture-r3` 已真实运行并完整导出四路 scope/native；RUN 查询零，四板请求值
  与锁存值一致，十边沿块实际生效，bridge_samples 均为 1。NO1–NO4 分别提交
  34/105/577/412 块，分批规划 155/324/1746/1264 次，缓存失效 29/3/5/9 次；
  快路成功补给 0/8/35/20 次，wall 最大 31.208/47.500/62.128/48.532 µs，
  调用与样本分别 59/99/330/185 且逐板相等，超候选预算全零。此为有限窗口，非 WCET。
  输出专项仍 FAIL，四板均 STARVED；NO4 一次 submit 拒绝，其余零。退休时
  原末下降沿完整界已过去约 0.47/0.81/1.32/2.13 ms。末态计数不能确定每次
  DMA 就绪或缓存失效的因果顺序，详见 `capture-r3/refill-retirement-analysis.json`。
- NO1 启动邻域可见 190 个脉冲（总准入 340），块内 171 个周期误差约
  −0.181..+19.858 ns，18 个块界误差约 +28.469 ns..+56.200 µs；15 个块界
  超过仅用于诊断分组的 100 ns。20 ns 采样及插值不证明纳秒物理精度；本轮
  模型修订 31 次，不足以逐边界归因。固定映射已采用而块界波动仍在，不能再
  把全部波动归于每块重采 bridge，也不能把跨启动窗口与前轮作为受控 A/B。
  图与原件见 `no1-review/capture-r3/`；保留历史内部 PI −9..+9 ns 基线。
- 四板最终 STOP 58/16/58/59 均已应用，输出 PIO/DMA 关闭；示波器恢复 STOP/
  EXT/NORM，已由 `final-stopped-state.json` 另行读回核验，scope error 为零。
  配置能力与实际采用已验证，连续性未通过；下一切片优先核对
  模型更新使可编辑后缀重建的代价，以及 DMA 退休至末沿之间的实际补给期限。
  参数试验可以同源码 STOP 修改后复采，无须烧录/P0；不通过放宽断流或精度
  判据来宣称完成，不提前修改 NO1 PI。
- 契约 `VDC-PRIORITY-01` v14 保持 pending；独立 C11 接受稳定语义范围，原件
  `design-review/c11-v14-semantic-independent-r1.json`。扩大块容量不增加 DMA
  退休后 FIFO 余量；固定映射不消除真实模型更新造成的边沿变化，连续性和
  相对 NO1 同序物理边沿差仍为后续专项门禁。
- 代码提交 `95006a3`，真实 pre-commit 核验当前 staged 指纹及 P3 凭证通过；
  文档单独提交，外来 TDMA 工作区文件保持未暂存，未 push。

### VDC-PROGRESS-20260918-003：相位内缓存交接与 SRAM 热路径验证

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  本条数字为证据快照，非产品事实源。保留 NO1 历史内部 PI 残差基线及现有
  算法；内部残差、输出周期误差与跨板同序边沿差仍分别验收。
- 完整 TDMA callback 因相位剩余时间不足跳过时，原本连同尾部输出补给一起
  跳过。现按 `PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES` 在同一 TDMA
  相位内重新核时，只接续仍有效的私有后缀；不生成首块、不桥接或反解、不借
  后续相位/GUARD。IRQ 在短服务期间关闭，剩余 ingress 配额不变；完整服务
  skip/start-miss、失败返回和 full-only phase_run_count 保留。短服务 runtime
  单独按候选预算检查并进入原期限账，生命周期/STOP 和 CAS 整次所有权保留。
- RUN schema 追加短服务调用、成功准入、空缓存、body 上界与 caller wall/超时
  统计。caller wall 不含后续统计报告本身，后者由相位尾期限检查覆盖；延迟报告
  复验 request，不能污染新请求。CAS 拒绝及重新准备后的统计清零不能当作完整
  采样；快照只在 STOP/退休后导出，RUN 查询仍为零。
- 首轮证据根 `out/HardwareAcceptance/20260918/dpll-run-fallback-r1/`：联合 host
  237 项、Release 和同源码四板 quick P3 通过，P3 为 PASS_WITH_WARNINGS，
  189.453 秒，25 INFO/18 WARN/0 ERROR/FATAL；严格/closed-loop/realtime 仍 false。
  有限输出专项 FAIL，四板均 STARVED。真实短服务准入 44/70/15/40 次，但最大
  caller wall 为 327.152/345.884/342.832/376.292 µs，分别超候选预算
  211/244/73/147 次；保留调用/报告差 2/1/0/1，不将入口接通当作短交接达标。
  四板 STOP44/43/44/44 已应用，原生每板 76 条记录及原始 SCPI 字段独审一致。
- 同轮 NO1 波形 190 个完整脉冲，142 个块内周期误差约 −17.993 至 +19.535 ns；
  47 个块界中 44 个超过百纳秒，极值 −129.641/+139.315 µs，至少 18 个异常
  不能各自对应一次独立模型切换。采样二十纳秒，不能作独立纳秒精度认证；
  启动波形窗口未覆盖最终耗尽，不以窗口内未停输否定末态 STARVED。单轮块数
  或间隔变化不构成受控 A/B，逐块原因仍待定位。独审见该根 `no1-review/`。
- 后继证据根 `out/HardwareAcceptance/20260918/dpll-run-sram-r1/`：仅将客户端、
  后端补给和身份/时钟读取热函数显式放置主 SRAM，并禁止重新内联到 XIP；
  保留算法、PIO/DMA、STOP、编码、PI 和候选预算。共享 SDK 叶函数仍须按最终
  链接审计，不使用全局 SDK/OTA 放置选项，不宣称仅凭源码注解满足预算。
- 新源码联合 host 504 项通过，独立 237 项通过；Release build
  `20260917171629` 双槽链接与安装包绑定通过。实际目标热符号共 18 个进入
  主 SRAM，主 RAM 扣 heap 后余量 22864 B，较首轮减少 8192 B（含布局变化），
  scratch gap 24 B，完整 RUN 栈 1644/3072 B，继承最大 2872 B。
  `memcpy`、`clock_get_hz` 和退休拉低输出的 SDK 叶函数仍在 XIP。
  源码指纹 `2aac1dc32955a9737d4582d558cf5e38da5a4dd886321f6f97f32a7a1711a23d`，
  包 SHA `64f93374ae051cc1be46a266bd6020d0334fb9a4bef8ee825697407dba6668bb`；
  最终资源及仅驻留属性变更的独立核对见新根 `runtime-review/`。
- 新根 `p3-r1` 为同源码四板 quick P3，PASS_WITH_WARNINGS，189.390 秒，
  25 INFO/18 WARN/0 ERROR/FATAL；四 START 精确 OK，STOP41 已应用，
  每板 14 条原生记录和有效 process-image 推进。严格/closed-loop/realtime
  仍 false，固定快速验收范围未扩大，匹配凭证和独审保留。
- 新根 `capture-r1` 有限输出专项仍 FAIL，仅四板输出 STARVED 断言失败。
  四板准入 2474/384/582/725 块；短服务成功准入 108/18/33/53 次，
  caller wall 最大 46.480/42.944/38.576/42.644 µs，535/109/157/213 次调用
  均有 wall 样本，候选预算超时为零。以上仅证明这次保留样本符合候选预算，
  不授予普遍 WCET；与旧轮初态不同，不以块数变化证明因果改善。
  本轮四板运行态准入拒绝均为零，末态仍 FIFO 空/TXSTALL/DMA idle，完整补给
  连续性未闭合。四板 START 精确 OK，RUN 查询零，STOP44 已应用、PIO/DMA
  关闭；每板 76 条原生记录、NO2–NO4 模型/相位末态与既有判据吻合。
  原始记录、波形和独审均在新根，不覆盖首轮超预算和断流失败。
- 新 NO1 波形仅覆盖 9896 个准入脉冲中的 191 个启动脉冲；143 个块内间隔
  误差约 −13.884 至 +19.579 ns，47 个块界中 39 个超过百纳秒，极值
  −107.260/+109.319 µs，大跳变仍存在。全程模型变化多于窗口异常数，不能再
  套用上一轮的“至少部分异常没有独立换模”计数下界，也不能反推全由模型更新
  造成。原生事件前缀晚于波形窗口，无法逐块绑定；采样仍不作纳秒精度认证。
  独立报告与冻结清单见新根 `no1-review/capture-r1/`。
- 下一 gate：按用户提出的连续长时间轴方向，在同一硬件 tick 坐标中递推
  后续编码，提前规划可修改后缀，以硬件剩余执行时间确定补给水位；保持已
  承诺前缀不可改写、DPLL 更新可及时接入及 STOP/代际取消。软件缓冲边界不应
  成为重新桥接或硬件重启边界；当前 PIO 并未每块重启，问题仍须由完整规划
  服务可用性、时间映射和模型接续分别定位。保留 NO1 PI，不授予连续输出、
  百纳秒锁相或 VDC 发布完成。

### VDC-PROGRESS-20260918-002：Core1 自主周期释放与断流复核

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260918/dpll-run-release-r1/`；以下数字为本轮
  证据快照，非产品事实源。保留用户指出的 NO1 旧内部 PI 小残差基线，不改 PI。
- 仅将 `core1_realtime_entry()` 的周期等待由 SDK `sleep_until()` 改为
  `busy_wait_until()`。旧 Core1 路径是共享 alarm pool 的非 RTOS WFE 分支，
  不是 Core1 上的 RTOS 任务调度；新最终链接函数直接读取硬件 TIMER0，
  没有共享锁、alarm 或跨核唤醒依赖。绝对周期、迟到追赶、STOP 周期重基和
  Flash lockout poll 保留，不增加 GUARD 工作或修改静态相位预算。
  空闲期主动轮询增加；该结构修正不等于实测周期抖动或输出连续性已达标。
- 实际入口 host 覆盖绝对释放、迟到、Flash poll 顺序、四档周期重基及回绕，
  联合 14 项通过。Release build `20260917162329` 双槽 ELF/BIN/包绑定通过；
  静态 RAM 增量零，RUN 栈 1684/3072 B，继承最大 2872 B，扣 heap 后 RAM
  31088 B。源码指纹 `d2681712175cd88e90a8db29511609001ffeaa52df42ef1125f39d9e15955dcf`，
  包 SHA `731af8c3320c3cfb453086a3d9b80dc76e9b2aca8a8887f58c31df8a51713b71`。
  独立实现、最终链接与资源审计见 `runtime-review/`。
- `p3-r1` 首次失败保留：NO3 START 返回旧 helper 的超时替代文案，并非真实
  ACK 或已完成读回；其原生记录有 ARM 基线、未触发且零采样，另外三板各有
  完整记录，但不能据此证明四板有效数据闭环。四板 STOP41 已应用。
  未改源码和门槛的一次有界 `p3-r2` 复测为 PASS_WITH_WARNINGS，195.125 秒，
  24 INFO/21 WARN/0 ERROR/FATAL；四板 START 精确 OK、各有 14 条记录及有效
  process-image 推进，凭证匹配当前源码。严格/closed-loop/realtime 仍 false；
  首次 START 未成功的具体原因未证明，不称已修复启动偶发问题。
  代码、入口测试及匹配凭证已提交 `48ac0c6`，真实 pre-commit 源码指纹门禁通过；
  另一工作流的 TDMA 文件修改未暂存或提交。
- `capture-r1` 同源码有限静默专项仍 FAIL：四板 START 精确 OK，静默 11.016 秒、
  RUN 查询为零；分别准入 44/106/133/558 块，缓存命中 32/89/116/463 次，全部
  STARVED。末次服务间隔为 7.763620/1.301848/3.488548/7.188696 ms；NO2/NO3
  各有一次补给拒绝。退休观察均为 FIFO 空、TXSTALL、DMA idle/remaining zero。
  去掉共享唤醒依赖未消除长服务空窗，不以跨初态计数比较推定改动改善或恶化。
  本轮未出现前轮的 FOLLOW 末态原因断言失败，不追改前轮结果。
  四板 STOP44/43/44/44 已应用，输出 PIO/DMA 关闭；示波器完成冻结采集，
  波形及退休分析在 `capture-r1/`，独立 NO1 分块复核在 `no1-review/`。
- NO1 新波形有 176 个完整脉冲，对应全部 44 块，末脉冲后窗口内保持低态。
  132 个块内周期误差约 −0.443 至 +19.361 ns；43 个块界误差约 −122860.057
  至 +5580 ns。40 个超过百纳秒的偏差全部在块界，至少 27 个不能各自归因于
  一次模型切换；聚合计数不能标定具体异常对应的更新事件。
  其中单次大负跳变比前轮大，但非受控 A/B，不能直接归因本轮 release 修改。
  原生前缀仍晚于波形及输出耗尽，不能逐块归因 PI、bridge 或模型变化。
  采样间隔二十纳秒且使用插值，不能当作独立纳秒精度；内部 PI 小残差基线保持。
- 下一 gate：核对 `app_realtime_run_phase()` 整相位迟到跳过与 TDMA 尾部输出
  服务的依赖，在既有静态预算内保证必要快速交接并定位不可分割服务空窗；
  不能只扩大 DMA 块或将可取消软件缓存当作硬件库存。后继只读候选与指令模型
  见 `out/HardwareAcceptance/20260918/dpll-run-handoff-r1/design-review/`，尚未实现。
  再独立隔离块间 bridge 重投影和模型连续性；不重复 P0、不改 OTA、不调 NO1 PI，
  不宣称连续输出、锁相或 VDC 发布完成。

### VDC-PROGRESS-20260918-001：有限后缀预规划与 NO1 块边界定位

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  午夜前的 host/Release/设计证据在 `out/HardwareAcceptance/20260917/dpll-run-prefetch-r1/`；
  后续硬件及独审根为 `out/HardwareAcceptance/20260918/dpll-run-prefetch-r1/`。
  以下数字均为本轮证据快照，非产品事实源。
- DMA 忙时提前准备一个私有后缀；不推进已准入序号/末边沿，命中后省去重新
  bridge 采样和模型反解。准入仍复验 STOP、身份、模型、前一末序号/下降沿及
  完整时钟配置；原子取消意图由 owner 清缓存，准备/终态清理，每次最多一轮
  规划和一次准入。保持 TDMA 内原服务入口、NO1 PI、桥接公式和后端安全边界。
  RUN schema 追加预规划、成功缓存准入和模型/尾部失效计数，仅退休后读取。
- 联合 host 193 项、独立 client 121 项和综合独审 187 项通过；Release-r1
  build `20260917155805`，两槽 ELF/BIN/包一致；缓存净增静态 RAM 184 B，
  RUN 调用链栈 1684/3072 B，继承路径最大 2872 B，扣 heap 后 RAM 31088 B。
  来源与限制见新根 `runtime-review/`，不是实测 WCET 或输出精度证明。
- 新根 `p3-r1` 同源码四板 quick P3 为 PASS_WITH_WARNINGS，耗时 238.609 秒，
  25 INFO/18 WARN/0 ERROR/FATAL；继续保留原始严格质量失败。源码指纹
  `1cce4002f2d838bc58dd4688ce9602280cf19458ff740a33ae3f72dc15e26735`，
  包 SHA `a348f0dab29452d34574474be0c734912d6e610ae8776b5db923237a686775cf`。
  独审确认耗时增长集中于 OTA ACK 等待，本轮不改 OTA；复用线序和非 OTA 流程
  未增加。代码及匹配凭证已提交 `0903d87`，真实 pre-commit 硬件门禁通过；
  前轮诊断一并收敛，另一工作流的 TDMA 文件修改未暂存或提交。
- NO1 旧内部 PI 残差 −9 至 +9 ns 的基线继续保留。午夜前的只读
  `no1-review/no1-independent-review.json` 将两轮旧物理输出复核到四脉冲块交界：
  超过百纳秒的周期偏差全部位于交界，块内约二十纳秒内；至少部分异常不伴随
  模型切换。既有原生 trace 晚于这些波形，不能归因具体峰值或等同引脚锁相。
  逐块 bridge 重投影和模型换代连续性为后继独立切片，不先调整 NO1 PI。
- 新根 `capture-r1` 有限静默输出专项仍 FAIL：四板分别准入 319/161/71/43 块，
  成功缓存命中 251/138/63/39 次，提交拒绝均为零，随后全部 STARVED。
  末次服务间隔为 7.569644/2.070688/6.019932/5.979860 ms，各自跨过最终下降沿
  的完整时间界；FIFO 空、TXSTALL、DMA idle/remaining zero 的首次退休观察一致。
  预规划已经实际使用，但不能填补服务空窗；计数与旧轮不同初态不是受控 A/B。
  同轮另有三从 phase-endpoint 失败保留：旧采样器要求 FOLLOW 末态 STOP reason，
  实际为 BINDING reason；离线复验末态模型序号、频率和相位基点一致。
  取消来源尚未区分 RUN 绑定变化与 STOP 交接，不改脚本判据或把原失败改判通过。
  四板 STOP44 已应用、输出 PIO/DMA 已停，RUN 查询为零，示波器恢复 STOP/EXT/NORM。
- 新波形 NO1 可见 190 个完整脉冲，142 个块内周期误差约 −1.273 至 +19.768 ns，
  47 个块边界误差约 −3079.889 至 +1480.167 ns；42 个超过百纳秒的偏差全部在
  块边界，至少 18 个不伴随模型变化。采样间隔二十纳秒且使用插值，不能由此
  授予独立纳秒精度；NO1 的断流晚于此短窗，以退休原件确认。
  原生 NO1 前缀仍晚于波形窗口，具体逐块映射原因未证明；独审与图见新根
  `no1-review/capture-r1/`。新缓存不消除原有块边界阶跃，未改 PI 或桥接算术。
- 下一 gate：在 HAOFV 静态相位预算内独立解决快速交接和长服务空窗，并核验
  FIFO 源退休时机；随后以逐块证据隔离 bridge 重投影与模型连续性。
  不扩大本切片 quick P3 范围、不重复 P0 寻优、不改 OTA、不宣布连续输出或锁相。

### VDC-PROGRESS-20260917-031：有限输出补给诊断与断流分型

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260917/dpll-run-refill-r1/`；完整本轮记录保存在
  `checkpoint-031.txt`。以下数字均为本轮证据快照，非产品事实源。
- 新增有效服务/成功准入间隔、首次退休前 PIO/DMA 观察及 PREPARED/RUNNING
  客户端结果计数；不改变准入或调度，只在退休后导出。联合 host 169 项通过，
  Release-r2 与独立资源审计通过，同源码四板 `p3-r1` 为 PASS_WITH_WARNINGS，
  耗时 185.812 秒，19 WARN/0 ERROR/FATAL；严格质量失败保持原值。
- `capture-r1` START ACK 失败保留，NO1–NO3 零块、NO4 有输出后取消；未放宽
  超时 sentinel 判据。`capture-r2` 四板各提交 16/458/172/269 块后 STARVED；
  NO3/NO4 最后服务空窗 7.779484/6.081336 ms，跨过同一末尾边沿的时间区间；
  NO1/NO2 前次服务仅剩约 197–206/264–273 µs，各有一次计划后提交拒绝。
  四板首次退休观察均为 FIFO/DMA 耗尽；运行态 ring/model/bridge 暂忙计数为零。
  示波器确认 NO1 的 64 个完整脉冲后中断。专项仍 FAIL，四板 STOP47 已应用，
  PIO/DMA 已停，静默窗口无实时查询；离线复核与原始波形位于 `capture-r2/`。
- 用户确认按粗锁定、精锁定、完全锁定逐级推进，目标和统一物理判据见 TODO；
  先连续输出，再粗锁定。NO1 的微秒级边沿变化须区分参考、模型校正与输出路径，
  不能仅由晶体 ppm 规格推定随机抖动或判定锁相失败的唯一原因。
- 用户补充旧多板 PI 中 NO1 基本在 ±20 ns。已查到 2026-09-14 的
  `baseline-r1-lock-review-r2/plots/dpll_residual_analysis.json` 内部 NO1 残差为
  −9 至 +9 ns，支持保留旧 PI 收敛基线；该内部量不能直接等同当前引脚周期抖动。
  下一步对照内部模型与实际边沿，优先分离时间映射和输出执行误差。
- 下一 gate：缩短补给服务空窗、提前准备下一有限块并做快速交接；另成独立
  host/Release/同源码四板 P3/输出专项切片，再缩小 bridge/enable 时间锚误差。
  本轮代码已验收但暂未提交：并行自回归检查器/测试修改使主工作区与验收指纹
  不同；隔离工作树尝试又受原始证据绝对路径约束，未绕过门禁或改写凭证。
  保留全部并行修改和已验收源码，待两项工作的提交边界协调后继续。

### VDC-PROGRESS-20260917-030：有限持续 PIO 输出与启动观察收敛

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260917/dpll-run-executor-r1/`。本节数字为
  验证快照，非产品事实源；有限输出切片不授予百纳秒精度、产品 RUN 或锁相。
  代码提交 `ad63f75` 已经真实 pre-commit 核验当前 P3 指纹；他人
  `components/tdma/src/tdma_flight_engine.c` 未修改、未暂存、未提交。
- 按 node-sequence 分支的预编码、资源预留及退休机制实现小型持续后端；
  当前只驱动 OUT1，统一 SYNC_IO 管理全部语义 IO 的边界不变。使用现有
  scheduled persona 的 PIO0 SM1、DMA2、八条指令及共享 arena 的八个源字；
  每块四个脉冲，所有 pull 在低态，高态不等 FIFO。DMA 源退休不代表边沿完成，
  模型更新只改新后缀，取消/断流/时钟异常/有限时长到期异步退休。
- VDC Core1 固定入口位于 TDMA owner 之后：STOP 锁存 delay 和请求；START
  后验证当前 session/role/clock/slot/schedule，再以整数反解与共同网格准备未来
  边沿。复核修正了外层尚未退休就被 Core0 覆盖、DMA 忙遮蔽绑定失效、ARM
  误当 START、首块前 STOP 遗留及完整诊断快照进入实时栈等问题。
  `SYSTem:VDC:OUTPut:RUN` 提交有限请求，`:STOP` 只提交取消，`RUN?` 仅退休后
  导出。真实 parser 测试发现旧 lexer 的数字尾空白截断，局部在副作用前拒绝，
  未改第三方 parser。Release-r2 的 signedness 编译失败保留，r3/r4 修复通过。
- 首版 owner/PIO/bridge 回归 109 项、客户端/真实 parser 62 项、既有固定输出/
  数学/相位回归 159 项通过。`runtime-review/` 对新调用链按地址建图，保留
  同名静态函数，未发现新 RTOS、堆分配、日志或阻塞依赖。future_raw 由
  非作者 `event_core` 独审：四项生产测试、10284 组有理数端点案例及 6250
  个内部样本通过，额外 raw 一拍上界不能删除；见独立数学报告。
- 首次 P3-r1 同源 `a93a1c0902cb424d17e90068323e184eead2694305ca9a3eba10f4bc04324d6b`
  完成，耗时 190.672 秒，25 INFO/18 WARN/0 ERROR/FATAL；原严格、closed_loop、
  realtime 为 false，diagnostic 为 true，分级 `PASS_WITH_WARNINGS`。
  四板各十四条原生记录、序号推进及 bad 增量零、STOP 已应用，独审见
  `runtime-review/p3-final-independent-review.json`。
- `capture-r1` 输出专项失败：四板均只准入首块，随后 CLOCK 退出，
  `anchor_after=UINT64_MAX`；这只能指向 raw/PC 初次观察无效，不能归因于
  时间锚跨度超限。没有新示波器触发，不能宣称出脉冲。三从末态相位提交
  33/43/50 次，有限原生前缀分别记录 27/31/35 次，末态与 DCO 模型一致；
  两种覆盖不可混写。零 RUN 查询、全板 STOP、示波器恢复 STOP/EXT/NORM。
  初版脚本冻结副本与采集记录 SHA 相同，失败原件不被后续复测覆盖。
- 启动观察修订为 enable 后一次 raw 观察、单次 PC、上界 raw，保留原始
  before 下界及 PC 必须已进入低段的规则，不轮询、不放松准入。新增独立
  PC/offset/raw flags 和两次原始读数，区别异常来源；RUN? 末尾追加字段。
  实际 C/MMIO/真实 parser 回归 113 项通过，独审连同编码/桥接/门禁共
  132 项通过。Release-r5 build `20260917145647`，A/B ELF→BIN→包一致；
  源码 `e7acef7796e738af1edd7a1da1f2fbf1f968f8e03b82b3960df1839081e0ac8a`，
  1267 文件，包 SHA `f6301d1e12d7839dc498e5e8e0153382f73cc2aa496dbfd0f489e3a1d1213acc`。
  新 RUN 限定路径栈上界 1572/3072 B，既有限定最大路径 2872 B，
  主 RAM 扣 heap 后余 31664 B、scratch 余 24 B；不以此宣称 WCET 或精度。
- 修订后 P3-r2 耗时 198.391 秒，25 INFO/19 WARN/0 ERROR/FATAL，
  `PASS_WITH_WARNINGS`；严格/closed_loop/realtime 仍 false，diagnostic true。
  四板序号及 accepted 增量 262–264，bad 增量零，STOP 代际已应用；同指纹与
  原始证据独审见 `runtime-review/p3-r2-final-independent-review.json`。
- `capture-r2` 首次启用通过：四板 `start_pc=27`、`program_offset=23`、
  `start_raw_flags=3`，已进入低段。输出周期 1 ms 时分别准入 293/117/215/86
  块、采用 31/1/2/0 次模型变化，后续均因 TXSTALL/STARVED 退休，专项 FAIL
  保留。计划/绑定拒绝均为零，不足以排除未计数 snapshot 暂不可读、DMA 尚未
  ready、客户端门禁冲突或整个 TDMA 相位被跳过，不能直接定因为 CPU 过慢。
  冻结 RAW 实际看见四路约 2 µs 脉冲；NO4 在窗口尾部约 80 ms 无输出，
  波形也证明中断，不能以多脉冲基本检查通过替代持续输出通过。
- 同固件 `capture-r3` 仅将输出周期改为 10 ms，TDMA 静态周期仍为 1.5 ms；
  有限静默窗口后四板输出正常 CANCELLED、PIO/DMA 停止，分别准入
  287/292/295/300 块，源退休 286/291/294/299 次，采用模型变化
  8/31/33/31 次。三从相位末态提交 26/28/26 次，与频率及 DCO 末态一致；
  原生有限前缀相位为 22/21/21 次。零 RUN 查询，四板 STOP 后导出，
  示波器恢复 STOP/EXT/NORM，专项脚本 PASS，耗时 56.609 秒。
  首次 enable 区间仍为微秒量级，bridge 区间也未达到百纳秒；该通过只授予
  本次低频有限输出与动态后缀采用，不把 1 ms 失败改成通过。
- `capture-r3/scope-run-analysis.json/.svg` 独立解码同一冻结 RAW：20 ns 采样、
  NO1 CH1 触发附近 -10 至 +190 ms，四路各二十个完整脉冲，脉宽中位约
  2.007–2.009 µs，无窗口内部长间隔；未覆盖整段十一秒。相对 NO1 最近边沿
  的中位差约 46.920/43.061/53.680 µs，未认证同 ordinal。约 80/160 ms
  三从相对差共同跳变与 NO1 自身短周期同位置，不能直接称三从共同漂移或
  算法变差。四路首脉冲各自对齐展示只用于比较脉冲形状，不是实际边沿重合。
- C11 稳定语义为 `ACCEPT_V13_PENDING_STABLE_SEMANTICS`，登记保持 pending；
  数学独审、实际指令测试、资源与硬件证据分别留存。下一 gate 为补给间隔与
  暂忙早退的有界观测，定位 1 ms 断流；随后缩小 bridge/enable 公共偏移并
  验证真正相位精度。原始失败和已通过低频基线同时保留，长期目标继续进行。
  最终九文档与实施事实独审为
  `ACCEPT_V13_PENDING_CONTRACT_AND_FINITE_10MS_EXECUTION_SCOPE`，原件
  `runtime-review/c11-v13-final-independent-review.json`；动态模型/原生/STOP
  独审见 `runtime-review/capture-r2-r3-independent-review.json`。

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

### VDC-PROGRESS-20260917-024：映射约束保留上限实板收窄与外部复测

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-map-retention-r1/`；以下数字均为
  本轮快照，非产品事实源或物理精度契约。
- 仅提高 `VDC_CLOCK_MAPPING_MAX_CONSTRAINTS` 的保留上限，当前实现为
  64 次；缓存仍是一组交集。原桥接准入、当前桥临时求交、矛盾拒绝、模型/时域
  重置、事务式提交以及跟随控制律保持。满额后的新约束仍参与当前事件，
  但不成为下一事件的持久约束；不增加数组、遍历、普通解析或 Core0 依赖。
- ORIGIN 原生版本随重放语义升级，布局和容量保持，代码头文件为事实源。
  固件编译期绑定 schema 与映射上限；解码器对历史 schema 2 固定使用 8 次，
  对 schema 3 固定使用 64 次，不以当前固件配置解释历史记录。从板记录不变。
  旧原件保持，真实历史源记录及从板原件均复核兼容。
- 映射专项 41 项、控制/投影/TX 集成 350 项通过。trace 三模块先通过 129 项，
  增加真实 C 原件版本误标拒绝后 origin 模块 37 项通过；后者含重叠，不相加。
  独立有理数 oracle 覆盖超过原上限的随机序列；第 64/65/66 条测试宽度为
  747/499/747 ns，保留标记为 1/0/0，证实瞬时额外约束没有泄漏到后续事件。
  满额重置、回退、空交集、耗尽和完整输出不变均验证。
  初次 host 命令路径错误、trace 临时父目录缺失以及旧凭证预检拒绝均保留；
  更正后通过，预检拒绝未产生硬件动作。
- Release build `20260917101448`、源码指纹
  `674a38125964995cee5560b6d27fef806a7f4df14bb41816b1351c0ed68b2a19`
  已完成双槽资源重建、同源码四板 quick P3 和原件独审。静态 RAM 增量零，
  主 RAM 余量 33040 B、scratch 余量 24 B；当前 persona Core1 最大栈
  2872/3072 B，无新增可达函数或调用边，不据此证明 WCET 或全负载预算。
  P3 用时约 178 秒，25 INFO/18 WARN，无 ERROR/FATAL；严格质量失败保留。
  代码已提交 `dc49df4`，真实 pre-commit 已核暂存源码与本轮 P3 凭证匹配。
- 第一段同窗原生采集通过：165 次 MATCH、34 条决定，三从实际更新 10/9/9 次，
  末态 1730/1909/4303 ppb，逐条与 Domain 模型一致。运行零查询，全部 STOP
  后读取。NO1 源前缀 76 条、约 122.04 ms，首条至 STOP 的已发布模型 token
  不变；不把查询包络中的启动变化当成整个跟随窗口的参考漂移。
- 第一段同一原件分别按上限 8/64 复算，平均编码宽度为 533.32/444.42 ns，
  收窄约 16.67%；尾部 12 条为 522/365 ns，收窄约 30.08%。新上限与实板全部
  记录精确一致，逐事件为原上限区间的子集。新结果平均分解为 latch 252.42 ns
  加映射歧义 192 ns；尾部为 254+111 ns。三从各一个同源事件核对正确，
  完整决定端点对为零。该短前缀不能证明全窗逐帧交付；不能用上轮不同输入
  的候选降幅替代本轮实际收益。图见 `analysis/source-prefix-width.svg`。
- 第一段末态的新鲜四通道固定输出采集与独审通过，原始块、整数输出计划、
  原生末态模型和全部批次完成状态一致。相对 NO1 的三从条件频差约为
  [−361,−275]/[−403,−301]/[−453,−345] ppb，仍偏慢，未锁相。
  这些界依赖采样单元和定频模型，不是已标定模拟误差界或绝对相位证明。
- 保持相同固件和已采用 DCO 的第二段原生采集独审通过：165 次 MATCH、24 条
  决定，三从实际更新 0/0/1 次，末态 1730/1909/4323 ppb；初态确实继承。
  NO1 启动前查询为 1843 ppb，首成功编码前改为 1694 ppb，此后至 STOP
  已发布模型不变。源前缀约 141.04 ms；同输入上限 8/64 均宽为
  833.95/546.32 ns，尾部 12 条为 793.33/375 ns，全部实际编码精确复现。
- 第二段新鲜固定模型波形与末态绑定独审通过，三从条件频差约为
  [−174,−95]/[−222,−156]/[−276,−172] ppb。相比第一段更接近零，但 NO1
  末态降低 149 ppb，三从仅 NO4 增加 20 ppb，不能解释为三从均主动继续
  收敛，也不作为跨初态的上限调整 A/B 收益证明。两段原始波形分别保留。
  所有采集运行零查询，四板及示波器最终 STOP、会话清空、PIO/DMA idle 已读回。
- 下一 gate：第二段 NO2/NO3 首组基线 62/64 的源区间宽 1647/1595 ns，
  保留到约 8 秒档；该档远端差分宽 2978/2926 ns、本地均宽 1 ns，
  仍跨零保持。后续源前缀尾部均值已降至 375 ns，但不会追改旧基线。
  两板末次决定已换成基线 8062，远端差分均宽 1738 ns，不归因于首组基线。
  8062 同时是上一组末次当前事件，由差分宽减去已知基线宽，两板均得到
  1331 ns；作为下一组基线后，末次宽度分解为 1331+407 ns。这只恢复宽度
  贡献，没有凭空补出缺失的 MATCH 或绝对端点。
- `analysis/baseline-width-study.json` 对同模型已记录 MATCH 做有限反事实：
  改用约 200 ms 后宽 375 ns 的基线 262/264，对同一约 4 秒处的当前 MATCH，
  NO2 频差界从 [−336,134] 变为 [−224,−44] ppb，NO3 从 [−355,102] 变为
  [−255,−75] ppb。局部差分保留两层真实整数取整，再与绝对端点差求交；
  20 次独立 Fraction 结果与真实 C `vdc_model_project_event_delta` 一致。
  这是同一记录对的候选估计，不是缺失 DECISION 的重建，也不预测改策略后的
  实际控制轨迹。下一切片据此验证有界 DPLL 基线选择/保留策略；不重新加入
  TDMA 首帧优化或重复校准前置。本轮未修改该策略，未消除漂移，ACK、
  实时模型输出、100 ns 锁相及一致 VDC 发布继续保持未完成。

### VDC-PROGRESS-20260917-023：同窗源记录核对与远端区间收窄候选

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-origin-follow-window-r1/`；以下数字均为
  实测或离线重算快照，非产品事实源或精度契约。本切片没有修改生产实现，沿用
  进度 022 的 build `20260917091644` 和源码指纹
  `0efc73091de851f479dfacf9016e1997292b7ef15ff959f20a4343fa1f96d164`；
  采集前核验既有同源码四板 P3 凭证，不冒充新构建或新 P3。
- 复用 schema 2 ORIGIN 和三从 MATCH/DECISION；新代际下四板 ARM ACK 均在
  START 前完成核对，有限静默采集期间零查询，全部 STOP 后分页读取、CRC
  复算并释放。流程约 29 秒；采集工具相关 host 34 项及两次无硬件预检通过。
  独审确认 165 次 MATCH、29 条决定；NO2/NO3/NO4 实际更新 2/1/3 次，末态
  1822/1963/4448 ppb，逐条与真实模型一致。四板及示波器最终停止状态另行读回。
- NO1 原生池满即冻结，共 76 条成功编码，源事件 69 至 198，覆盖约 129.04 ms。
  三从各有一个同源事件可精确核对，其远端区间与源编码一致；没有完整控制
  决定的两个源端点同时落在前缀内。因此不把前缀当成全窗逐事件运输证明，
  不据此重建所有决定的源端区间。逐板导出绑定与 19 项误绑定拒绝检查通过，
  原页/CRC/ACK/STOP 与对齐报告经独审核验。
- 更正参考变化解释：本轮 START 前与 STOP 后查询为 token 80→120、DCO 序号
  653→693、1905→1857 ppb，但全部 ORIGIN 均为 token 120/序号 693/1857 ppb。
  首成功源事件发生于终态模型发布约 22.76 ms 后；同 boot/session 的已发布模型
  token 只增不回绕，首条与 STOP token 相同，证明两者之间没有新的成功发布模型。
  这不证明物理振荡器恒定或未发布的瞬态不存在，也不能外推旧轮的中间轨迹。
  进度 022 的起止 MODEL 差仅是含启动过程的查询包络，不足以证明整个跟随
  窗口内参考持续变化；不再将扩充全窗遥测作为下一项算法试验的前置。
- `analysis/new-prefix-width-study.json` 以原始桥接记录和严格区间重新计算：
  `VDC_CLOCK_MAPPING_MAX_CONSTRAINTS` 当前上限精确复现全部 76 条。
  上限候选 8/16/32/64 的平均编码宽度为 1063.79/702.37/587.37/554.58 ns，
  尾部 12 条均值为 965.33/600.33/435.33/371.33 ns；各候选逐事件区间嵌套，
  无矛盾及模型/时域重置，独立有理数复算一致。
  当前平均宽度分解为 latch 252.32 ns、映射歧义 811.47 ns；增加保留次数
  收窄的是映射交集，不能消除 latch 宽度或直接证明物理锁相。
- 缓存保存一组交集，上限控制继续保留多少次约束，不是对应长度的数组。
  下一 gate：保持当前跟随响应，单独验证映射保留上限候选；覆盖模型/时域
  重置、矛盾拒绝及历史 trace 解码兼容，再做 Release/资源、同源码四板
  quick P3、原生决定和绑定末态模型的新鲜示波器采集。离线收益尚未导入固件，
  不宣称实时耗时下降、跨零消除或外部精度改善。
  本轮无新波形；外部剩余漂移仍以进度 022 的两段独立波形为准。
  ACK、实时换模型、100 ns 锁相和一致 VDC 发布继续保持未完成。

### VDC-PROGRESS-20260917-022：外部反馈驱动的有界跟随响应试验

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-follow-response-r1/`；以下数字均为
  本轮试验快照，非产品事实源或物理精度契约。
- 在进度 021 的已采用模型上保持原四分之一响应，追加有限静默窗口建立基线。
  独审核验 165 次 MATCH、29 条决定；三从实际更新 1/1/5 次，末态
  1167/1361/3598 ppb，NO1 前后 1492→1384 ppb。NO2/NO3 的同一 baseline
  确实走到后档后更新，不能把早档跨零保持解释成未接收或未执行。
- 追加基线的新鲜固定模型四通道采集通过：相对 NO1 的三从条件频差约为
  [−477,−402]/[−442,−388]/[−682,−603] ppb，仍偏慢。
  与旧窗口的差异含参考变化及继续运行，不是控制增益的隔离 A/B 结论。
- 单因素候选只调整 `priority_follow_delta()` 的最近零界响应，保留原区间、
  窗口、deadband、单步和总限幅、生命周期、连续 Domain 提交及 NO1 PI。
  从四分之一改为二分之一；这是当前策略试验，代码为事实源。没有缩窄远端
  区间或用中点代替确定方向，也不改普通 AUTO/RefMem 路线的控制策略。
- 实际倍率关系为 `R_new=R*(1e9+current+delta)/(1e9+current)`，不能声称误差
  精确减半。host 核对当前试验频率范围的方向、截断、饱和及不越零响应；
  软件可表示的极低倍率只证明数值/限幅安全，不扩展为全配置稳定性保证。
  静止数学轨迹不是硬件收敛或锁相证据。
- 候选八模块 417 项通过，按独审建议补充显式截断/饱和断言后跟随模块 57 项通过；
  导入后三个相关模块 82 项通过。以上有重叠，不相加为不同测试数量。
  实现独审确认仅响应比例和对应测试变化；采集器仅适配 STOP 后的决定复算，
  不增加运行查询。保留旧采集器和基线原件。
- Release build `20260917091644`、源码指纹
  `0efc73091de851f479dfacf9016e1997292b7ef15ff959f20a4343fa1f96d164`
  通过双槽资源重建及同源码四板 quick P3。可用 RAM 33040 B、静态增量零，
  当前 persona 条件下 Core1 最大栈 2872/3072 B；链接调用图无新增边。
  P3 为 24 INFO/21 WARN，无 ERROR/FATAL；相对进度 021 新增 T0 residence
  质量失败、对应执行反馈及继承的 T3 矩阵质量告警。有效输入/运输仍通过，
  不将旧或新增质量失败抹去，也没有隔离证明告警由增益变化造成。
- 第一段原生独审 165 次 MATCH、40 条决定，三从实际更新 7/8/8 次；
  末态 609/729/3078 ppb，NO1 791→793 ppb。逐条核到二分之一规则的真实
  Domain 采用，末次 +54/+52/+93 ppb；新鲜四板固定输出批次均完成。
  外部条件频差约为 [−473,−403]/[−538,−458]/[−577,−518] ppb，仍偏慢。
- 保持候选固件与已采用 rate 再运行一个有限静默窗口，独审确认初态继承。
  第二段 165 次 MATCH、38 条决定，三从再更新 8/8/9 次，末态
  1718/1894/4317 ppb。NO1 起止查询 793→1905 ppb，三从新增
  +1109/+1165/+1239 ppb；查询包含启动过程，缺少同窗轨迹，不能分配
  具体因果或将累计 rate 增加直接解释为消除静态误差。
  NO2/NO3 末次区间 [−875,13]/[−979,85] ppb 跨零保持；NO4 仍更新 +33 ppb。
  第一段末次远端差分区间宽度为 1610/1610/1606 ns；第二段为
  1598/1598/1334 ns，本地差分宽度均为 1 ns，按各段原生记录分别绑定。
- 第二段绑定末态模型的固定输出也成功，三从相对 NO1 条件频差约为
  [−456,−402]/[−469,−388]/[−464,−357] ppb。两段分别核原始块、模型及整数
  计划，不能跨不同模型合并区间。所有频差界均依赖采样单元/定频假设，
  不包含已标定模拟时变误差；两段斜率仍未消除，未授予精度达成。
  四板与示波器已实际 STOP，会话清空、PIO/DMA idle，示波器无错误。
- 当时下一 gate（后由进度 023 承接）：先关联同窗 NO1 参考变化与从板误差，核远端编码宽度和参考
  响应延迟，再决定下一单因素调整；不盲目继续提高增益。
  当前三段记录没有 NO1 的中间 rate 轨迹，起止 MODEL 不能补齐缺失样本。
  优先复用既有 schema 2 ORIGIN 的 rate/DCO/事件/编码记录；其有限容量满即冻结，
  先核同窗短前缀的实际覆盖，不能声称覆盖整个跟随窗口或先新增遥测。
  三栏原始边沿漂移图见本根 `analysis/relative-phase-three-runs.svg`，各栏按
  自己的模型独立归零；不同参考/初态，不作为受控 A/B 或绝对相位证明。
  不将 quick P3、频率方向或静止模型输出当成
  ACK、实时换模型、绝对相位、100 ns 锁相或一致 VDC 发布完成。
  代码已提交 `d365f2e`，真实 pre-commit 已核暂存源码对应 P3 凭证。

### VDC-PROGRESS-20260917-021：固定输出快照准入与轻量时钟资格

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片恢复用于算法纠偏的
  外部固定模型观测，不修改三从控制律。证据分别保留在
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r1/`、
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r2/` 与
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r3/`；以下数字为实测快照，
  非产品事实源。
- Core0 的初始准入、PIO 启用前复验及启用后复验仅对明确的 ring/model
  快照暂忙重读。每阶段采用 `VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS` 次数上限，
  首次完整观察不设新增墙钟有效期；首次明确暂忙后才启动
  `VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US`，仅判断能否发起下一次观察。
  已发起观察若完整证明有效，不因耗时而反转为失败；
  真正的模型、会话、时钟、自检或运行状态变化仍拒绝。后两阶段逐字绑定
  原模型，不重发硬件启动；运行 service、STOP 与状态读取行为保持。
  原原因码与固定状态字段保持兼容，新原因码区分各阶段暂忙、超时及调用上下文。
- 只读检查 ARM 中断/异常上下文，调用方屏蔽中断时拒绝，不替调用方开启中断。
  重读属于 Core0 诊断，不进入 Core1、RTOS 等待或普通负载搬运。重读预算不是
  被 IRQ 抢占时的硬 WCET，也不能保证启用后被拒绝的批次从未发出首沿。
- r1 软件、Release/资源、四板 quick P3 与原生专项通过，但外部采集未通过：
  首次 NO2、第二次 NO3 均为 PRE_DEADLINE；第二次 NO2 完成批次，NO4 STOP 后
  模型读取另有超时。原因码细分是真实收益，但不能因此声称恢复可靠采集。
  四板及示波器终态已独立读回；失败和原生记录保留。
  r1 原生独审为 165 次 MATCH、36 条决定，三从实际更新 8/9/10 次；不是锁相。
- r1 的 fixed clock 准入每轮读取整份 diagnostic，但实际只使用
  `configuration_supported && clock_ready`。r2 新增
  `vdc_timestamp_clock_configuration_supported()`，直接复用原 private 配置谓词，
  保留 ready acquire、平台、时钟源、PLL/XOSC、分频、timer/debug 等资格；
  不采集未被 fixed 准入使用的桥接时间戳。原桥接和诊断函数保持原义，
  不缓存资格、不初始化硬件、不改变时钟或重读门限。
  源级成功路径约从 111 次 MMIO 降到 24 次，仅为访问计数，不是实测耗时或根因证明。
- r2 候选及导入后合计 175 项相关 host 通过，覆盖静态配置等价、非法配置、
  不支持的平台、只读与无初始化、计数器采样失败、快照恢复/耗尽、时限边界、
  会话/模型切换及失败释放；r1 另有模型 owner 10 项通过。
  实现和设计独审分别保留在两根 `design-review/`。
- r2 Release 两槽资源重建通过：可用主 RAM 33040 B、静态增量零，当前 persona
  条件下 Core1 栈最大 2872/3072 B、scratch 余量 24 B；轻配置函数位于 XIP，
  未进入 Core1 调用图。资源审计修正了同名 local 符号按名称覆盖造成的误报，
  多重集与链接 map 相符；不将这些资源数字当作 WCET 或全负载证明。
- r2 同源码四板 quick P3 通过，25 INFO/18 WARN，无 ERROR/FATAL；原生专项
  独审为 165 次 MATCH、31 条决定，三从实际采用 9/10/10 次，末态模型一致。
  但首次外部复采 NO2 即 INITIAL_DEADLINE、request=0，未启动硬件；
  无有效四通道波形。证据不能区分该次具体读前/读后超时或证明特定抢占根因。
- r1/r2 曾把整段观察墙钟耗时加入准入，可拒绝本来有效的完整观察。
  r3 改为上述额外重读准入预算，保留失败原件；候选和导入后各 178 项 host
  通过，独审确认只有 validator 行为变化，轻量 clock 与运行清理保持。
  覆盖有效首读/有效重读跨预算、未知观察耗尽后不再发起读取及真实失效拒绝。
- r3 Release build `20260917084807`、源码指纹
  `c140d1ca1170d6373939b2fb03db10300843d059efaa91f0f7a67e70e5b68a06`
  已完成资源与同源码四板 quick P3：资源与 r2 相同，P3 为 25 INFO/18 WARN，
  无 ERROR/FATAL，WARN 集合与 r2 相同。原生独审 166 次 MATCH、33 条决定，
  三从各实际更新 10 次，末态 rate 为 1134/1281/3422 ppb，模型与原件一致。
  本轮只修诊断准入，不以采用次数或不同初始状态的运行比较控制律优劣。
- `scope-fixed-r1/r2` 连续两次新鲜四通道固定模型采集成功，四板每次均完成
  2048 脉冲，模型不变，PIO/DMA idle，运行查询为零；每轮约 30 秒。
  两轮全边沿条件模型均可行：相对 NO1 的 NO2/NO3/NO4 频差分别约为
  [−638,−559]/[−669,−592]/[−904,−831] ppb 和
  [−652,−541]/[−668,−534]/[−960,−849] ppb。独立复核原始块及整数计划，
  这是固定批次的剩余频差条件区间，不是已校准误差界、绝对相位或实时锁相。
  三从仍偏慢，不能把更新成功或内部区间跨零解释成无需调节。
- 本轮 NO1 原生窗口前后 rate 为 1625→1492 ppb，内部参考并非已证明恒定；
  旧轮与本轮的初始模型、温漂和参考轨迹未受控，不能将外部差异归因为诊断修复。
  外部结果只提供模型绑定的离线校验方向，不直接把区间中点写回 DCO。
  三从末次实际修正仍为 +32/+46/+36 ppb，方向与后续外部偏慢相容；
  由 `priority_follow_delta()` 对区间最近零边界采用当前响应比例得出，
  不是外部剩余频差的固定比例，也没有证据说明 DCO 符号算反。
  末次远端差分区间宽度为 1170/1158/1170 ns，本地宽度均为 1 ns。
  优先继续核验远端宽度与保守响应，但这不是已证明的唯一剩余频差来源。
  `final-stopped-state.json` 已实际读回四板 RING/PWM/PIO/DMA 停止、会话为零，
  示波器 STOP/EXT/NORM、错误队列为空。
- 下一 gate：保持已恢复的外部采集，继续以外部相位斜率对照内部估计及实际
  DCO 步幅，区分远端编码不确定性、参考变化和保守响应，每次只改一个算法因素。
  候选仅调整最近零边界的响应比例，须独立测试与四板 P3/专项；尚未实施。
  两轮原始边沿的独立归零漂移图见 r3 根 `analysis/relative-phase-drift.svg`，
  图中斜率仅作描述，不能代替全边沿约束或绝对相位证明。
  代码切片已提交 `51d4333`，暂存源码指纹由真实 pre-commit P3 门禁核验。
  实时换模型、绝对相位、接收/采用 ACK、100 ns 锁相和一致发布仍未完成。

### VDC-PROGRESS-20260917-020：外部全边沿约束与采集启动拒绝

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-external-feedback-r2/`；以下数字均为
  本次分析快照，非产品事实源。生产固件、控制增益和契约未修改。
- 用户确认用示波器真实反馈纠偏算法。外部数据用于离线核验估计方向、响应及
  剩余漂移；板上 NO1 PI 与三从本地跟踪继续走 Core1 确定性路径。SCPI 仅控制
  有界采集，运行中不查询，全部 STOP 后导出；不引入示波器在线依赖。
- `external-review/all-edge-review.json` 独立校验进度 019 成功固定输出的四路
  原始块、模型及累计 PIO 整数计划，并对每路保留的全部边沿求周期约束。
  使用原始 ADC 过阈值相邻采样单元与累计取整误差，对所有边沿对取交集；
  拟合仅用于描述，不作为确定性误差界。各阈值和未建模误差分别保留。
  条件交集下 NO2/NO3/NO4 相对 NO1 分别约为 [−237.31,−150.00]、
  [−111.67,−18.75]、[−376.08,−294.50] ppb，方向均为增大 rate。
  这是旧采集的新离线分析，不是本轮取得的新波形。
- 上述条件界不含已标定的模拟时变延迟、采样孔径或温漂界。灵敏度分析中，
  仅额外假设每沿 ±1 ns 误差，NO3 的方向即未决；NO2 在假设 ±5 ns、NO4
  在假设 ±10 ns 下仍偏慢。这些是假设预算，不是仪器校准结果。
  若参考及振荡器保持不变，已采用 rate 为 c、实测频率比为 R 时，理想增量为
  `(1e9+c)*(1/R-1)`；不能将外部区间中点直接写入当前模型，或重复叠加已采用增益。
- 当前已部署 build 的 `scope-fixed-r1/r2` 均保留 FAIL：三从各完成固定批次，
  NO1 START 返回超时；第一轮末态 reason=4、尚无编码摘要，第二轮 reason=5、
  已有编码摘要及剩余 transfer_count=4087。两次分别定位到不同启动拒绝阶段，
  不合并为模型错误或示波器配置错误，不认定快照争用为已证根因。
  两次均未取得有效四通道波形，不能用来比较漂移变化。
- 原 `final-state` 检查要求 transfer_count 清零，因中止批次保留未消费计数而失败；
  原件保留。后继 `final-stopped-state.json` 实际读回四板 RING 停止、会话清空、
  PWM 关闭及 PIO/DMA idle；NO1 批次失败仍明确保留。示波器为 STOP/EXT/NORM、
  错误队列为空。硬件停止与批次成功分别判定，没有放宽波形采集成功条件。
- `origin-review/origin-review.json` 另行只读重放旧源端原件：未观察到 model token
  重置；增加有限约束数量可能缩小远端编码宽度，但尚无当前 build 的单因素验收，
  不改 token 生命周期，也未将该候选与本轮外部分析混入同一实现。
- 下一 gate：先细分固定输出启动拒绝、恢复可重复的新鲜四通道采集；随后绑定
  同一模型和观测窗口，对照内部区间、外部斜率与实际步幅，每次只改一个算法因素，
  经软件、资源、同源码四板 quick P3 及专项验证。绝对相位、实时换模型、100 ns
  锁相和一致 VDC 发布仍未完成；原严格质量告警继续保留。

### VDC-PROGRESS-20260917-019：连续事件共钟差分与外部反馈纠偏

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-external-correction-r1/`；以下数字均为
  本次证据快照，非产品事实源。
- 修改前同固件、同参数的 `native-r1` 通过：NO2/NO4 在后档实际调整
  +10/+25 ppb；NO3 最终后档仍为 [−501,+61] ppb，不能视为已同步。
  三从各有完整原生记录，全部决定经独立 CRC/分页、Fraction、生命周期和
  末态模型复核。NO3 此次本地差分宽 2001 ns、参考差分宽 2490 ns；单纯增加
  增益不能改变跨死区的零步准入。NO1 包围窗端点为 1829→1838 ppb，不能据此
  假定具体配对期间参考恒定。见 `native-review.json`、`control-review.json`。
- 此轮修改前 `scope-raw` 成功，`scope-fixed` 在启动请求返回超时后失败；
  原件保留，不能声称取得新的固定模型物理频差。该次清理后的停止状态见
  `final-board-scope-state-r2.json`，后继硬件验收状态需按各自终态另行核验。
- `VDC-PRIORITY-01` 修订为 v8 pending，明确 typed 两端共同指向
  `F(floor(X(event)))`。新增 `vdc_model_project_event_delta` 在同模型、共同
  observer 起点及已准入的共钟生命周期内抵消公共偏移，保留先本地整数 ns、
  再 DCO 缩放的两层外向取整；继续与原绝对端点差求交，空交集仍拒绝。
  旧 helper、微秒阶梯余量及旧测试保持原义；远端区间、增益、死区和调度不变。
- 候选经隔离工作区验证后逐文件核验导入：原有 425 项 host 与新增 7 项通过，
  新套件含 14457 组真实 Domain/Fraction 数值例；非整数 ns 时钟必须保留内层
  取整的反例已纳入。数学、实现与 C11 独审分别见 `continuous-delta-review.json`、
  `implementation-review.json`、`contract-review.json`。
- Release 两槽资源重新分析通过：静态 RAM 增量为零，余量 33040 B；当前 persona
  的 Core1 栈仍为 2872/3072 B、scratch 余量 24 B。候选在本次时钟下的本地差分
  算术宽度至多 1 ns，不是物理精度或 WCET 证明。
- 代码已提交 `2d4656e`（`feat(vdc): use common-clock continuous event deltas for typed control`），
  staged 指纹经真实 pre-commit 与四板凭证核验通过，文档分离提交。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，23 INFO、22 WARN、零 ERROR/FATAL；
  build `20260917062327`，源码指纹
  `304cbe74baf3737b42e2ce67baed602faf7d5b79bd2faeefda9cd46217a1f906`。
  包、两槽 ELF、四板 OTA 及原生短帧经独审一致。相较上一切片警告净增六项：
  T1 SCK 候选覆盖和 T3 replay 余量分别新增质量/执行告警，NO4 VDC 新增超时、
  隔离及 max-runtime 告警，NO1 RefMem deadline 少一项。NO4 VDC 最大值由
  188→408.608 us，quarantine 由 8→9；是真实新增质量劣化，尚无单变量因果证据，
  不称为“基线不变”或无回归。严格质量仍未通过，见 `p3-review.json`。
- 部署后 `native-candidate-r1` 通过：三从 165 条 MATCH、35 条决定独立 CRC/Fraction
  重放通过，每条本地差分宽度为 0–1 ns，远端上下界保持原件；三从各有十次真实
  DCO 更新，末态为 1348/1568/3715 ppb。OTA 后从零修正起步，不能用更新次数
  相较修改前的变化推断收敛速度；频率/物理效果仍按各自窗口验证。
  见 `native-candidate-review.json`、`native-comparison.json`。
- `sustained-candidate-r1` 的后继静默持续窗口通过，末态为 NO1/NO2/NO3/NO4
  1919/1920/2204/4424 ppb；NO2/NO3 末次实际调整 +20/+39 ppb，NO4 末次未决。
  原件独审确认本轮请求重置计数，DCO 序号差分为三从实际新增 21/23/21 次更新；
  末匹配距末接收约 4.81/4.18/4.14 ms，无 observer 溢出，STOP 模型一致。
  见 `sustained-candidate-review.json`。
  此处证明持续输入及真实控制仍可运行，不授予锁相、全负载 VDC 或发布完成。
- `scope-raw-candidate/scope-fixed-candidate` 成功取得新 SINGLE 波形，采用上述
  末态模型。主控在相同电压阈值下拟合 NO2/NO3/NO4 相对 NO1 为约
  −196/−73/−338 ppb；拟合仍有剩余漂移。采样/取整条件界与所采用修正的预测
  相容，但两次有限采集不是同时观测，不能忽略温漂或把小频差估计当作精度认证。
  图为 `scope-fixed-candidate/physical-dco-comparison.svg`；它展示有限固定模型
  执行效果，不证明运行中模型切换、绝对相位或 100 ns 锁相。
  `physical-review.json` 独立复算与该拟合相容；NO2/NO3 的小修正增益方向未被
  条件界单独分辨，NO4 正向修正可分辨。示波器仅覆盖约 0.2 s，不能把板端完整
  批次计数当成全部脉冲均被外部观测。NO3 末次内部区间与后继外部拟合点并非
  同一时间窗或同一采用前模型，不能声称该内部区间包含外部点或据此拟合校准常量。
- 固定输出采集工具仅追加失败后的诊断：必须四板 output/RING STOP 都实际返回
  OK，才在清空会话前读状态；不重发 START，不放宽成功判据，见
  `scope-capture-review.json`。此前失败是本轮 NO1 MODEL 拒绝，和上一轮 NO2
  资源/参数拒绝分开记录；单次快照争用仍只是候选解释，不能归咎示波器配置。
  最新 `final-board-scope-state-final.json` 已确认四板全部停止，示波器
  STOP/EXT/NORM、错误队列为空。
- 下一 gate：对照物理剩余漂移、远端区间及 NO1 参考轨迹，选择下一个单因素
  频率收敛切片；接收/采用 ACK、绝对相位、失联恢复、一致发布和严格调度质量
  仍未完成。不重复实现已经验证的本地共钟差分，也不把实际 0–1 ns 算术宽度
  改写成跨板物理精度。

### VDC-PROGRESS-20260917-018：有界长窗口产生真实后档 DCO 调整

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-longwindow-adoption-r1/`；以下实测数字
  为本次快照，非产品事实源。此前隔离的长窗口候选现已导入当前主线并验证，
  不再把它列为未部署候选。
- 代码已提交 `0e7cede`（`feat(vdc): extend bounded follower frequency observation windows`），
  staged 源码指纹与四板凭证由真实 pre-commit 核验通过；文档分离提交。
- `VDC_PRIORITY_FOLLOW_*_INTERVAL_NS` 在原有早期机会后增加有界后档；
  `VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS` 限定每个基线的计算次数。
  只在误差区间仍无法支持调整时保留同一本地模型基线；最新票据年龄、完整事件
  与会话/模型/观察器锚点检查不变，STOP、换模型、过期、错误次序和无效区间
  仍取消或重建。远端只提供两个已准入端点，不声称得到 NO1 瞬时频率。
- `vdc_model_scale_elapsed_ns()` 用商余拆乘和外向取整避免较长间隔乘法溢出；
  `priority_follow_rate_interval()` 在无符号域扣除偏置后才转换，保留完整量化界
  与已准入绝对端点差的求交。没有放宽单事件绝对投影年龄，也未修改增益或死区。
  五档跳过不补算、BUSY/票据替换不退还机会。数学/生命周期独审见
  `design-code-review.json`，425 项 host 通过。
- Release 两槽独立资源复核通过：无新增静态主 RAM，余量仍为 33040 B；
  当前 persona 的 Core1 深链重新分析为 2872/3072 B。新阈值表位于 Flash，
  工作区与诊断对象未扩容。此为链接资源证明，不是 WCET 或全负载证明。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，25 INFO、16 WARN、零 ERROR/FATAL，
  严格实时质量仍未通过。源码指纹
  `4a749af74b7aa84fb0238ea437703a3ca4d1a2f06c93a2c7df05231c4a6f3e5e`，
  build `20260917054056`；包、两槽 ELF、四板 OTA、原生短帧与分级凭证独立一致，
  见 `p3-review.json`。已测线序和 TAP 复用，未改 OTA、未操作 NO5。
- `sustained-r1` 保留 FAIL：三从 observer 无故障且末段匹配有效，但 NO2/NO3
  STOP 后模型读取超时，不能用后继数据补填。该轮 NO4 末决定在约 4 s 窗口
  得到 [−1315,−550] ppb，实际调整 +137 ppb，DCO 序号与真实模型一致。
  同参数 `sustained-r2` 完整通过，三从均无 observer 故障、末态模型一致；
  长档等待期间允许最后决定早于最后接收，不能将其误判为断流。
  VDC quarantine 仍存在，不声称完整 VDC 调度恢复，见 `sustained-review.json`。
- 补充 `sustained-r3` 保留 FAIL：NO4、NO3 START 成功后，NO2 START 仅返回
  超时占位 ACK，NO1 尚未 START；没有真实状态读回或有效持续窗口，随后四板
  STOP。`sustained-r4` 完整通过，约 40.297 s 内三从末段匹配、模型一致且
  observer 无故障；NO4 在 8.000475282 s 得到 [−566,−122] ppb，实际 +30 ppb，
  DCO 序号 43→44。NO2/NO3 末次仍为 4 s 未决区间，不冒称最终后档失败。
  见 `sustained-followup-review.json`。
- `native-r1` 的静默原生专项通过：三从各 63 条记录，其中 55 MATCH、8 DECISION，
  跨度约 10.94 s，无 dropped/observer 故障；全部 24 条决定的分数运算、实际
  DCO 序号、残差、生命周期及末态模型经独立原件 CRC/分页重组复核。
  NO3 同一 baseline 65 实际经过 1/1.5/1.8/4/8 s 五档；最后一档约 8.002 s，
  区间 [−813,−213] ppb 支持真实 +53 ppb 调整。NO2/NO4 在约 4 s 分别调整
  +3/+10 ppb。此处证明原来未决的观察可经后档产生真实调整，不宣称锁相。
- `capture_longwindow.py` 复用既有 STOP、CRC、会话及有效记录判据，保留有限
  临时许可，运行期间不查询；4/8 s 档是否自然出现只作事实记录。容量规划是
  典型估算，不是最坏上界，满容量与丢记录仍失败。原生决定未携带每条 raw
  端点，因此全部记录复算区间/量化宽度，只有末条利用 STOP 快照完整复算
  raw 差分投影；不虚构缺失历史。见 `capture-review.json`、`native-review.json`。
- 第一组 `scope-raw/scope-fixed` 成功，采用模型 NO1/NO2/NO3/NO4 为
  +1676/+1416/+1864/+4016 ppb；固定输出相对 NO1 的频差拟合约
  −456/−324/−536 ppb，与采用修正的预测相容。相较进度 017 的拟合幅度下降，
  但 NO1 模型、温度和观测时刻不同，不作为单变量因果或连续收敛证明。
  固定模型输出仍不能证明实时换模型、绝对相位或 100 ns 同步。
- 补充物理采集未完成：`scope-raw-r2` 成功，`scope-fixed-r2` 因 NO4
  `model_unchanged=2` 失败，该值表示模型快照读取争用，不是已证模型改变；
  `scope-fixed-r3` 在 NO2 固定输出请求超时后失败。两次失败及清理原件保留，
  不用相邻模型字段相等追认采集，不宣称追加运行后的物理漂移继续下降。
  最新 `final-board-scope-state-r2.json` 确认四板 RING、PIO/DMA、PWM 和会话
  均停止，示波器 STOP/EXT/NORM、错误队列为空。
- `next-step-review.json` 复核保守步幅为靠零端点的四分之一，实际采用后重建
  基线。NO1 在原生与后继持续观测包围窗内分别由 1641→1676、1676→1829 ppb，
  但缺少具体配对窗口内的参考轨迹。不能把剩余频差仅归因于增益或量化。
- 下一 gate：继续 `VDC-FAST-003`，对照后档未决区间、NO1 参考变化及外部剩余
  漂移，确认后续频率收敛；随后补齐接收/采用 ACK、相位精度和一致发布。
  示波器外部反馈用于核验实际输出误差符号、调节方向和响应；同一模型/事件
  窗口绑定内部区间与物理相位斜率后，先区分最终档仍跨死区的分辨率问题、
  稳定参考下连续同号小步的响应问题以及参考变化，再选择单一纠偏切片。
  采样配置与 STOP 后导出留在调试侧，Core1 保持确定性处理；有限固定输出
  波形只支持执行效果核验，不能替代运行中闭环轨迹或绝对相位验收。
  不移除量化界或以区间中点伪造确定性。VDC 隔离和 observer 服务预算独立
  跟踪，不将当前通过扩大为全负载运行。

### VDC-PROGRESS-20260917-017：调度入口迁入 SRAM 与外部剩余漂移复测

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-observer-backlog-r1/`；下列数字均为本轮
  证据快照，非产品事实源。此次仅改变 `app_realtime_run_phase()`、
  `app_priority_counts()`、`app_priority_deadline()` 的主 SRAM 放置，不改变
  控制算法、静态表预算、迟到准入、隔离规则、DMA 容量或估计窗口。
- `baseline-sustained` 在无原生 trace 的静默窗口仍失败：三从 observer DMA
  积压达到 66/74/78 字，最大服务间隔 22.416/30.060/38.854 ms，末次匹配比
  末端接收早约 35/37/29 s；不能把末态 DCO 当作全窗持续跟踪。只排除“必须开启
  trace 才会失败”，并未证明唯一根因。结构复核见 `event-review.json`。
- 实际生产调度边界等 host 回归 158 项通过，Release 两槽链接与独立栈复核通过。
  迁移代码节为 3028 B，计入对齐后主 RAM 实际增加 4096 B，扣最小堆后余
  33040 B；当前启用 persona 的 Core1 栈分析仍为 2872/3072 B，scratch 余量
  24 B 不变。作用范围和排除项见 `code-review.json`、`resource-review.json`。
- 新源码四板 quick P3 为 `PASS_WITH_WARNINGS`：25 INFO、18 WARN、零 ERROR/FATAL，
  严格调度仍未通过；原始失败不改写。源码指纹
  `493ab59cd83fca6fe65e844c739281aae597ebf6e25e77cc8d9ef23cbb346e15`，
  build `20260917051203`；包、两槽 ELF、四板 OTA、原生记录及凭证独审一致，
  见 `p3-review.json`。代码与匹配凭证已独立提交 `ec9b0b0`，真实 pre-commit
  核验通过。不重复测 P0T，复用已确认线序和 TAP 输入。
- `after-sustained-r1` 在 NO4 START 时因 ACK 不匹配退出，未形成观测窗口；
  四板 STOP 原件保留。返回文字中的“verified by state readback”是现有工具
  在 timeout 时生成的占位说明，不是本轮实际读回证明；不能用计划时长或包装层
  零查询计数追认该失败。后继两轮沿用原参数与原判据，未放宽 ACK。
- `after-sustained-r2/r3` 均通过：三从 observer 无故障，最大积压为
  8–10 字、最大服务间隔约 4.1–4.5 ms，末段匹配与末次真实 DCO 决定均有效，
  停止因果为 STOP。主机命令包围时长分别约 44.094/40.328 s，名义采集等待
  均为 40 s；不将端点证据解释成连续残差轨迹。首次失败及两轮比较见
  `review-comparison-final.json`。
- 对照限制：基线 quarantine 为 8，候选为 9；候选额外隔离了超时 VDC，
  第二轮 VDC run_count 不再增加。两种固件的 ARM-lifetime VDC 背景最大值均为零，
  该字段不是调用次数，不能证明实际负载相同。可保留本切片用于 TDMA/DPLL 调试，
  不能独归因于 XIP，也不能宣称 VDC 调度恢复或完整预算通过。observer 自身
  批次耗时仍可能超过 VDC/DPLL 相位预算，此项独立保留。
- `scope-raw` 与 `scope-fixed` 成功，四板固定输出均完成全部脉冲，模型前后一致，
  四通道由 NO1/CH1 正沿新单次触发，静默采集后统一读回。实际采用频率为
  NO1/NO2/NO3/NO4 的 +2778/+2164/+2635/+4686 ppb；相对 NO1 输出频差拟合约
  −822/−696/−980 ppb，与采用修正的预测相容。两类波形属于先后采集，温漂和
  模型差异保留；不能将相较进度 016 的变化独归 SRAM 或宣布 100 ns 锁相。
  见 `scope-fixed/analysis.json`、`scope-fixed/physical-dco-comparison.svg`。
  最终四板 STOP、反馈会话归零，示波器 EXT/NORM/STOP 已实际读回，见
  `final-board-scope-state.json`。
- 下一 gate：继续 `VDC-FAST-003`，将真实输出剩余漂移与内部区间、最终档位
  保持原因联合复核，再评估隔离且未部署的长窗口候选。VDC 隔离和 observer
  服务成本另作有界切片，不能用扩大环或放宽准入掩盖；备用细分诊断仅完成
  `diagnostic-design.json` 设计，尚未实现。固定模型输出不替代实时采用、绝对
  相位、ACK、恢复和一致 VDC 发布。

### VDC-PROGRESS-20260917-016：固定已采用模型的连续物理输出

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片按用户要求用外部示波器反馈
  校核 DPLL，证据根为 `out/HardwareAcceptance/20260917/dpll-fixed-output-r1/`。
  主控原件/指纹复核入口为 `main-summary.json`，最终硬件状态为 `final-board-scope-state.json`。
  代码、测试及匹配四板 P3 凭证已提交 `d2021b8`，真实 pre-commit 核验通过；文档分离提交。
  下列测试、容量与时间均为证据快照，非产品事实源；此处不授予锁相或 VDC 发布完成。
- 新增 `vdc_dpll_manager_fixed_output_start()`：只在 Core0、TDMA 已停止且当前非零会话
  对应真实 committed model 时准入。启动前后复验模型与时源，不退回本地裸时钟；正常
  seqlock 争用保留批次，实际模型或运行上下文改变则取消。SCPI `SYSTem:VDC:OUTPut:FIXed`
  只配置触发有限批次，查询保留所采用的模型身份、编码参数、硬件终态与完成计数。
- `sync_io_rate_schedule_generate()` 对累计边沿执行逆频率量化，避免逐周期取整吞掉小
  修正。复用现有 PIO0 scheduled-trigger persona、DMA 和共享工作区，有限批次由硬件
  连续执行，无逐沿 Core0 重装。真正完成还要求最后下降沿后的 PIO PC、DMA 与 FIFO
  共同满足终态；不能以 FIFO 空单独宣布完成。固定模型输出不自动采用后来模型，实时
  换模型及绝对相位另验。
- 独审发现并修复三项：正常模型读争用误取消、准备期旧接口夺取共享工作区、新失败
  请求混入旧批次统计。准备期抢占的红测试保留；最终独立资源/取消测试及相关综合
  回归通过，详见 `test/coverage.json`、`host-final.xml`、`review/code-host-final-review.json`。
  Release 与链接资源复核通过；新增静态 RAM 为 344 B，已启用 Core1 栈上界仍为
  2872/3072 B，数字仅为本次链接快照。通用运行状态 getter 不读取固定输出的 Core0
  缓存，也不在 Core1 分配其完整状态对象。
- 首轮四板 quick P3 为 `PASS_WITH_WARNINGS`，严格质量未通过；后继 `native-r1`
  整体失败保留。三从都有 typed 参考输入；NO2/NO3 的 observer DMA 环覆盖使观测
  失效、跟踪取消，不能把零调整解释为无需调整。NO4 原生决定与真实 DCO 一致，
  但记录跨度不足专项判据；固定输出当时未启用，不据此推定该新增输出路径的因果。
- `raw-r1` 已取得四通道原始时钟。`physical-r1` 在 ARM 前因新增 STOP handler 未
  输出成功 ACK 而超时，保留失败原件。真实 handler 红测试复现；修复为既有
  `scpi_port_result_ok()`，修复后相关 host 75 项通过。新源码 `p3-r2` 为
  `PASS_WITH_WARNINGS`，无 ERROR/FATAL；严格启动屏障告警保留。增量 build 字符串
  沿用缓存，实际版本由源码 `a07aa4e055f389019e81f011f5d199d73d285d1a27072c233ef6976f6cee770f`、
  package、四板 OTA 凭证与新 ELF 联合绑定；独审已逐字节核对包内两个应用与 ELF。
- `native-r2` 三从均有真实 DCO 更新，全部决定与末态模型一致；NO4 后段仍发生
  observer DMA 覆盖，整体专项 FAIL 保留，不能追认为持续跟踪通过。
  `physical-r2/r3` 分别在 NO3/NO2 启动准入被拒绝；状态为资源或参数拒绝，现有
  原件不能区分快照争用与资源，不能宣称根因已证。STOP、资源表和失败原件保留。
- 有界复试 `physical-r4` 成功：四板固定模型身份前后一致、各完成全部 2048 脉冲，
  DMA/FIFO/末沿终态及全部 STOP ACK 核验通过。与 `raw-r2` 的完整 DCO 字段相同，
  两轮均为 NO1/CH1 正沿新单次触发、四通道同帧 RAW、采集期间零查询。最终停止
  输出、反馈会话归零，示波器恢复 EXT/NORM/STOP。
- 外部快照（拟合值，非精度契约）：NO2/NO3/NO4 原始相对频差约 −223/−527/−2860 ppb；
  固定 DCO 实际输出约 −2409/−2144/−4686 ppb。NO1/NO2/NO3/NO4 当时采用
  +3786/+1598/+2096/+1984 ppb；实际变化与原始频差乘模型频率比例的预测相容，
  确认本批次执行方向和幅度，并未消除相对漂移。不能把原始 PWM 再加一次修正当成
  实测，也不能从当前差值单独裁定 PI 增益或长期稳态；原始与固定采样分轮，温漂等
  系统误差尚未完全计入。图与分析在 `physical-r4/physical-dco-comparison.svg`、
  `physical-r4/analysis.json`；独立多阈值/分段重算与完整原件绑定通过，见
  `physical-r4/independent-final-review.json`。首次离线分析的 NumPy 布尔序列化失败亦保留。
- 裸 PWM 不包含 NO1 的正修正，不能从裸时钟→固定输出的相对负频差增大推定从板
  调反。以此次 NO1 修正不变、从板修正仍为零作离线反事实，预计三从起点约为
  −4.0/−4.3/−6.6 ppm，现有从板正修正会缩小这部分差值；此起点是模型推算，
  不是同时实测。原生窗口中的已留存 NO1 模型均为相同频率修正，尚无该窗口内
  参考持续变化证据。短时运行、保守步长与量化区间、NO4 观测中断均须分开评估。
- 下一 gate：用已闭合的外部频率观测对照 NO1 参考和三从采用轨迹，
  定位跟随滞后、宽区间未决与 observer 服务积压，再评估隔离的有界长窗口候选；
  不直接用此次静态差值写校正值或放宽量化界。实时换模型、绝对相位、持续锁相和
  VDC 发布仍待验收；observer 连续性失败不扩大为固定模型输出的新增锁相门禁。

### VDC-PROGRESS-20260917-015：外部原始频差与现有 DCO 修正对照

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。用户授权以真实示波器反馈纠偏。
  已用现有固件的连续硬件 PWM、NO1/CH1 正沿触发取得四路同帧冻结 RAW，证据为
  `out/HardwareAcceptance/20260917/dpll-scope-raw-r2/`。采样期间零查询，全部输出停止后
  导出；四板时源配置与模型前后一致，原始块、脚本和独立复算均保留。
- 以下为本轮快照，非物理精度事实源：原始频差相对 NO1，NO2/NO3/NO4 约为
  −235/−430/−2867 ppb；计入当前模型后的推算残差约为 +1/−194/−1068 ppb。
  NO2 暂不调整有依据；NO3 的保守采样界仍包含零；NO4 的负剩余误差方向在保守
  采样界内仍明确，支持继续正向补偿。原始 PWM 不受 DCO 调节，此推算不是实际 DCO
  输出、绝对相位或锁相证明。
- 失败保留：首次请求低于既有硬件测频接口下限；扩大记录时水平偏移读回不符及未
  观察到新 WAIT 均拒绝，不把旧 STOP 波形当新采样。最终示波器恢复 EXT/NORM/STOP，
  四板停止、PWM 关闭。采用 NO1/CH1 触发是因为当前固件未启用 RJ45 单脉冲命令；
  用户 OUT4 到 EXT 的接线不代表该命令实际产生过触发。
- 有界长窗口候选已完成主机验证，隔离于 `out/worktrees/dpll-follow-longwindow`，未部署；
  为归因，当前主树继续采用已验收估计策略，先补固定模型的物理输出证据。

### VDC-PROGRESS-20260917-014：计数回绕伪歧义修复与持续跟踪

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片修复实际中断 DPLL 输入的
  TDMA observer 缺陷，未调整 FOLLOW 增益、估计档位或真实量化界。以下数字均为本轮
  证据快照，非容量、时序或精度事实源；证据根为
  `out/HardwareAcceptance/20260917/dpll-observer-lift-r1/`。
- 代码、测试与匹配 P3 凭证已提交为 `061ddc5`；真实 pre-commit 已核验暂存源码。
- 修复前同固件 `dpll-priority-longwindow-r1/sustained-r3/` 运行期间零查询；
  NO2–NO4 原始 recovery 均记录 `TDMA_EVENT_LIFT_AMBIGUOUS`、fault bits 为零，
  有效事件在约 34.36 秒停止。该时刻对应 PIO 计数周期；临时许可和 observer
  有限期限尚未到期。原运行 FAIL 保留，不归因为 RJ45 接触或码元错误。
- 连续 DMA 不一定提供 FIFO-empty 见证，前事件 age 下界可长期停留于起点；旧算法
  随总运行时长扩大相邻事件候选范围，误纳入多一整圈的候选。`lift_sample()` 仅对
  非首事件使用历史 `anchor_bounds + elapsed` 与原 age 求交，保留加法溢出、空交、
  首事件及真正长间隔多候选拒绝。公共 lift、PIO、DMA、状态结构与有效期限不变，
  不伪造 empty 见证、不清故障、不新增持久 RAM 或重试。
- 独立真实 feed 原版在 ordinal 22908 的事件复现 reason 11；同输入修复后通过 60000 个
  事件、跨两次回绕。最终独立 36 项、累计 676009 个 feed 事件通过，含真实长 gap、
  首事件、近 uint64 边界、STOP/restart、fault、部分批次及随机 PIO 指令计数。
  host 的两圈模拟不扩大生产有限运行许可。
- 联合 host 初轮为 1041 通过、一个失败和 24 个编译错误；原因是两份旧 fixture
  未跟进实际 RX/observer 拆分及公开时钟声明。补真实调用链、核黑窗仅 observer
  服务并保持 OTA 全部排除，修正 stub linkage 后原 25 项定向通过。原失败保留，
  合并覆盖 1066 项，不表述为一次全绿运行。
- App A/B/Boot Release 与 Flash 检查通过；独立 linked 审计静态 RAM 增量为零，
  预留堆后主 RAM 余 37480 B、scratch gap 24 B。observer 深链含 IRQ/异常为
  1000 B，event service 其他较深路径为 1240 B；既有总体已审最大仍为
  2872/3072 B。此为局部资源检查，不代替 WCET 或全部运行水位。
- 首轮四板 quick P3 为 `PASS_WITH_WARNINGS`，无 ERROR/FATAL；其后 `sustained-r4`
  三从 observer 到约 40.38 秒、MATCH 到 event 40088，FOLLOW 均为预期 STOP 12，
  不再因回绕 binding 退休。NO3 STOP 后 model getter 超时，整体专项仍为 FAIL；
  NO2/NO4 模型核对成功不能替代缺失的 NO3 原件。四板 STOP 和配置恢复完成。
- 最终测试指纹 `p3-final` 为 `PASS_WITH_WARNINGS`，202.078 秒，25 INFO、14 WARN，
  无 ERROR/FATAL。原严格结果为 false；保留启动 barrier 超时、NO1 反馈超窗、
  NO1/NO2/NO4 receive_missing 及 VDC/RefMem deadline 增长。本轮 T1/T3 无告警，
  不以分级通过声明严格运输或调度通过。
- 最终 `sustained-r5` 专项通过，40 秒静默、完整流程 52.187 秒，运行期间零查询。
  三从 observer 到约 40.39 秒、MATCH 均到 event 40088，FOLLOW 为正常 STOP 12；
  末次决定与真实 Domain 模型全部一致。NO2/NO3/NO4 实际更新为 0/0/23 次，
  零调整为 64/65/29 次、拒绝为零，末频率为 0/0/+1552 ppb。前两板宽区间跨零
  不能解释为已同步；四板 STOP 和配置恢复完成。r4 的失败不被本轮通过覆盖。
- 本轮 build `20260917030314`，最终源码指纹
  `53edd7d19c68785746b99d5ba767b1a9c6ae1b493f4992c17efcc90614009eac`，1231 文件；
  包 SHA256 `891a1d3ca3ceea1d8e76c0fc8dcaa82902afa817a5693020f35da2cfbc910b8e`。
  首轮 P3 的测试源码指纹另保留于 `receipt-r1.json`，不代替最终凭证。
- 随后同固件 `native-r1` 已继续主线观测：三从原生记录均完整读回、CRC 有效，
  末次原生决定与 FOLLOW 快照一致；NO1 origin 记录完整重放。NO2 STOP 后模型查询
  超时，整体专项仍 FAIL，不以原生记录替代缺失末态。原生分析可独立使用：NO2/NO3
  各 12 次零调整、四组基线的最终档位仍跨零；NO4 在不变本地模型的约 5.04 秒片段
  中，模型残差端点斜率界为 `[-1802,-315] ns/s`，随后仅调整 +11 ppb。
  这是同模型残差证据，包含远端模型变化，不能直接认作物理振荡器频偏。
  图与逐段区间见 `native-r1/analysis/typed_residual_dco.svg` 及 `analysis.json`。
- 此输入连续性切片已获得跨回绕实板证据；下一最小候选为在真实误差界与增益不变下
  延长未决基线，并先解决长间隔投影乘法和比值的溢出边界，再经独立回归与四板 P3。
  具体计划见 `next-step-analysis-r2.json`；候选尚未实现，不扩大 trace 池或静默丢决定。
  保留真实量化界评估频差分辨率和持续漂移。末态记录不能证明全窗斜率、逐圈必达、
  物理输出锁相或 100 ns 精度；不重复 P0T，不将启动首帧优化重新作为 DPLL 前置。

### VDC-PROGRESS-20260917-013：NO1 共钟区间收窄与原生同事件验收

本轮完成 NO1 typed 新事件的有限共钟桥接求交；不是锁相完成。以下数字为本轮
实测快照，非容量、时序或精度事实源。证据根为
`out/HardwareAcceptance/20260917/dpll-priority-clock-r1/`；独立数学、测试、源码、
资源及 C11 审查原件在同日 `dpll-priority-mapping-r1/`。

- Core1 保留有限半开约束，完整事件端点经真实 Domain 映射后与原投影求交；
  容量及跨度以 `vdc_clock_mapping.h` 为准。只在最终复验和编码成功后提交，
  同事件编码不变；模型/期限重建、矛盾退休、STOP 取消均有真实 TX 测试。
  不改变 Domain now、执行器、FOLLOW 量化界或每帧独立 latch。
- 新 origin 原生 schema 使用原维护池，从新 SYNC 的 Core1 空缓存 ACK 开始，
  连续记录全部成功贡献。FULL 固定最后记录对应的缓存，不随之后 TX 改写。
  旧从板 schema 及 STOP/read lease/CRC/RELEASE 保留，SCPI 运行期间零查询。
- 软件验收合并覆盖 878 项相关测试，通过记录见 `host-summary.json`。原广测
  835 通过、一个旧 TX SCPI fixture 缺新增头依赖；补真实 include 后定向通过。
  独立专项 85 项含 5124 组 Fraction 输入及真实 Domain/TX/原生记录，不把
  provider 假投影测试当数学证明。全部初始失败原件保留，未伪称单次全绿运行。
- App A/B/Boot Release 与 Flash 检查通过。独立 linked 深链审核：新增静态
  RAM 540 B，预留堆后主 RAM 余 37480 B，scratch gap 24 B；共享池仍 7600 B。
  TDMA→TX→mapped projector 加 IRQ/异常为 2068 B，origin trace 深链 1584 B，
  既有已审最大仍 2872/3072 B。局部资源证明不代替 WCET 或全部运行水位。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，221.203 秒，25 INFO、13 WARN、
  无 ERROR/FATAL。本轮保留 TDMA explicit startup barrier timeout、NO1/NO3/NO4
  receive_missing、VDC/RefMem deadline 增长；T1/T3 本轮未报告告警，不能照抄
  前轮失败。`strict_gates_passed=false`，不得写严格运输/调度已通过。
- 首次 `clock-short-r1` 专项通过，8 秒静默，完整流程 28.687 秒。NO1 连续
  76 条中 75 条收窄；**同事件单桥宽度中位 1687 ns，实际编码中位 863 ns**，
  中位减少 824 ns。原区间范围 1331–3875 ns，编码范围 391–1687 ns。
  全部原始桥、真实 DCO、实际编码及最后缓存独立重放一致，不取中点冒充真值。
- 三从与 NO1 原生窗口各有一条完全相同事件匹配：NO2 event 72，2647→863 ns；
  NO3 event 55，2163→1159 ns；NO4 event 46，2651→1687 ns。接收到的 remote
  上下界逐字相同。该证据只覆盖重叠原生事件，不泛化为所有帧单圈必达。
- NO2/NO3/NO4 的 MATCH 为 37/37/36，DECISION 为 10/7/7；真实 DCO 更新
  **5/6/7 次**、零调整 5/1/0、拒绝均零。序号及频率分别为 1→6、0→+395 ppb；
  1→7、0→+262 ppb；1→8、0→+2240 ppb。末次原生决定与真实 Domain 末态一致。
  NO4 完整频差区间从 `[-6576,-2199]` 到 `[-3915,-435] ppb`，仍在纠偏。
- 三从全窗残差端点变化区间分别为 `[-15854,-6934]`、`[-21603,-7971]`、
  `[-28267,-20163] ns`，仍有负向漂移。不能据更新次数或区间变窄宣称锁相。
  原生分析与图见专项 `analysis/typed_residual_dco.svg`；逐板区间见
  `comparison.json`，同事件核验见 `same-event-comparison.json`。
- 全部 STOP、ACK、CRC 读回、共享池 RELEASE 与配置恢复完成。STOP 后调度表
  仅为累计快照，缺 START 前差分，不能将其峰值归因于本轮新增映射或宣称 WCET。
  后续继续收窄频差判定与验证持续漂移收敛；不重复实现共钟缓存，不重跑 P0T 寻优，
  不引入启动首帧精细证明作为 DPLL 前置。

本轮 build `20260917021736`，源码指纹
`e211b774f0186eb807458e98ead1d79282a313d7a51e238155ca64c0230159a1`，1230 文件；
包 SHA256 `3cb1ce47136fa9abb94806e9651029baf4f3f31c88c9b659e7dca6632f1551fb`。
`VDC-PRIORITY-01` v7 经独立 C11 保持 pending；长期目标与物理精度尚未完成。

### VDC-PROGRESS-20260917-012：同模型基线保留与有限档位复评

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。本切片仅在宽频差区间与死区相交时延长同模型观测，保留全部真实误差界。
- 代码与匹配 P3 凭证已提交为 `4a3bc40`，真实 pre-commit 核验 staged 源码通过。
- `priority_follow_prepare_core1()` 在既有完整端点上限之后检查有限档位；未到档位不做
  相关投影或频差除法，进入计算前即消耗所有已到达档位。错过档位不补算，BUSY 与新鲜
  票据替换不退还机会。`priority_follow_apply_core1()` 只在区间与死区相交、未全落入
  死区且还有后档时保留原基线。自己的 DCO 更新、模型/生命周期改变、STOP、最终档位、
  到期、限幅或失败应用均保持既有重建/取消语义；事件消费和快照格式保持原界。
- 档位与上限引用 `VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS`、
  `VDC_PRIORITY_FOLLOW_SECOND_INTERVAL_NS`、`VDC_PRIORITY_FOLLOW_FINAL_INTERVAL_NS`、
  `VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS` 和 `VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS`。
  新私有状态使用工作结构原有尾部填充，不占诊断 reserved 或新增 wire 字段。
- 相关软件回归 410 项及新增独立窗口测试 18 项通过。首次相关回归为 400 通过、10 个
  fixture 编译错误：旧 model owner fixture 未接入上一切片新增的两个 trace wrapper
  入口。补充仅验证 guard 外执行及调用顺序的 seam 后，10 项复跑通过；真实 recorder
  行为仍由原生 trace 测试覆盖。新增测试首轮 15 项通过，补边界时 C harness 曾触发
  misleading-indentation 编译失败，修正测试换行后完整 18 项通过；失败原件均保留。
- 独立窗口测试连接真实 matcher、控制器、Domain 和 recorder，原生两次决策保留同一
  baseline，先跨死区不调整、后档真实采用。计数探针覆盖等待不计算、BUSY、未决替换、
  漏档、三档耗尽、完整上界、重复事件、窄死区、限幅、拒绝和生命周期取消。将实际
  `0ade494` 控制器替换进临时 TU 后，三个关键用例均失败，未改工作区生产代码。
- App A/B/Boot Release、Flash link 与独立 linked 资源审查通过：主 RAM 余 38020 B、
  scratch 余 24 B，准备/应用完整链含 IRQ 和异常帧分别 584/768 B，既有启用路径最大
  2872/3072 B，均未增加。这不是实际运行水位或 WCET 证明。
- 当前源码四板 quick P3 为 `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=23/19/0/0；
  build_id=`20260917014743`，源码指纹
  `7291856b177a20715b9322d80da05cbdef38e36b382a2181f288763fd61addb7`，包 SHA256
  `694c27984df8b96ea5805d5887582e5865c561c774cff7bfb02cd9d822ac7385`。
  复用已测线序。本轮 T1 SCK 质量、T3 新测 SCK 行选择和 TDMA startup barrier 均失败，
  已有兼容参数及基本运输仍可用，按固定范围保留 WARN；四板 receive_missing 与
  VDC/RefMem deadline 增长也保留，不能宣称严格训练、连续性或调度通过。
- `window-short-r1` 首次专项通过；运行八秒零查询，无 GPIO 诊断输出，完整配置、采集、
  STOP 后原生读回与恢复流程约 25 秒。三从 MATCH 分别 38/39/38 条，各 12 条 DECISION；
  同一基线的后续决策分别 7/8/8 次，每基线最多三次，实际间隔跨入后档。原件无丢弃，
  分页及整文件 CRC、末次 FOLLOW 与 Domain DCO 一致，采集池显式 RELEASE。

| 板卡 | 真实更新 / 零调整 | DCO 序号 | 实际频率修正 | 频差区间全宽范围 |
|---|---|---|---|---|
| NO2 | 2 / 10 | 1→3 | 0→−98 ppb | 2934–7383 ppb |
| NO3 | 1 / 11 | 1→2 | 0→−22 ppb | 2761–6985 ppb |
| NO4 | 2 / 10 | 1→3 | 0→+23 ppb | 2583–8000 ppb |

- 原生同基线证据直接证明后档采用：NO2 的 baseline 1900 从约一秒的
  `[-711,4306] ppb` 到约 1.503 秒的 `[268,3489] ppb` 后采用 −67 ppb；NO3 的
  baseline 3695 首档 `[-1528,3450] ppb`，约 1.827 秒为 `[88,3031] ppb` 后采用
  −22 ppb。NO4 的 baseline 3714 首档 `[-3629,1028] ppb`，约 1.811 秒为
  `[-2945,-69] ppb` 后采用 +17 ppb；后一个基线再次延长后采用 +6 ppb。
  数据见 `window-short-r1/retained-baseline-applies.json`；各组前后保持实际本地模型，
  没有从缺失的 MATCH 基线重造端点。实板 DECISION 只证明已完成决策的档位，包含
  BUSY/替换的全部准备计算上限由独立 host 覆盖。
- 后档频差宽度确实收窄，但仍为微秒级输入对应的千 ppb 量级。全窗模型 residual
  变化区间 NO2/NO3 为正、NO4 为负；长稳、物理输出采用、相位/100 ns 均未完成。
  前轮与本轮启动、模型和输入不同，不能据跨轮斜率或更新数量宣称算法独立消除了漂移。
- 证据根目录：`out/HardwareAcceptance/20260917/dpll-priority-window-r1/`，固定验收范围
  见 `acceptance-plan.json`；软件为 `software-summary.json`、`coverage.json`、原始日志
  和 JUnit，源码、资源及 C11 v6 独审分别为 `code-review.json`、
  `resource-independent-review.json` 与 `c11-review.json`。实测入口为 `p3/`、
  `window-short-r1/input-probe.json`、`comparison.json` 和 `analysis/typed_residual_dco.svg`。
  独立 `hardware-review.json` 重算二进制、分页、CRC、频差与同基线档位，核对真实
  DCO 链、运行零查询及 STOP/释放/恢复，结论为
  `PASS_SCOPED_BOUNDED_SAME_MODEL_WINDOW_SLICE`。
- 下一 gate：继续 `VDC-FAST-003`，按既有纳秒事件模型证明并收窄 NO1 参考投影的
  TIMER0/TIMER1 共同映射区间，保留每帧独立 latch 和真实量化约束；有限缓存与原生
  分解证据放在同一功能切片验收，不以额外诊断或首帧优化阻塞主线。既有本地差分量化界
  和 Domain 提交/调度逻辑保持，不把粗服务时刻读数与事件纳秒模型坐标混用。后续继续
  实际输出采用、ACK、单圈期限、物理精度及 VDC 正式发布。四板最终 STOP，配置恢复，
  未操作 NO5、未修改 OTA；保留他人 flight engine 工作区改动。

### VDC-PROGRESS-20260917-011：相关本地差分缩界与原生决策验收

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。本切片收窄本地实际输出间隔的估计界，保持绝对 MATCH、远端区间和控制律。
- 代码与匹配凭证提交为 `0ade494`，真实 pre-commit 已核验 staged 源码指纹。
- 同一绑定、observer 起点和本地 committed model 下，raw 差消去公共起点；保留
  `VDC_MODEL_CORRELATED_DELTA_QUANTIZATION_NS` 的双侧量化余量，在 DCO 缩放前计入。
  新候选与原绝对端点差求交，空交、非正下界、生命周期或模型不一致均拒绝并重新建立基线。
  两端须在模型有效起点之后；不将 NO1 逐帧 latch 重装和独立 bridge 当作公共误差消去。
  新除法仅在完整 `VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS` 窗口后执行，等待路径不增加此计算。
- 软件回归 366 项及独立差分/原生记录测试 38 项通过，后者含 20088 组数值输入，
  覆盖真实 Domain 映射、TIMER0 所有微秒内整数相位、正负 rate、边界、空交及取消。
  同事件宽 bridge 的 host 对照由 10014 ns 缩至 2002 ns；实板跨轮观测不等价于此同事件对照。
- App A/B/Boot Release、Flash link 与独立资源审查通过。静态 RAM 净增零，余 38020 B；
  完整准备链含 IRQ/异常帧由 544 B 增至 584 B，既有最大启用链仍为 2872/3072 B。
  此为 linked 静态栈分析，新增有效窗口计算的实际 WCET 尚未完成。
- 当前源码四板 quick P3 为 `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/14/0/0；
  build_id=`20260917012507`，源码指纹
  `221b62e15eedb43d0fbe4b7e8174f526cd0546f1b832eb66e63f70d94ba34069`，包 SHA256
  `7628e62b90f1ac7f2e0b1563b219f2b5e7087705624e1ac524d87352d6712632`。
  本轮 startup barrier 通过；feedback 序号超窗、receive_missing 和阶段 deadline
  质量失败保留，不把快速范围通过写成严格运输/调度通过。
- 首次 `delta-short-r1` 采样后，NO3 的 STOP 后模型查询返回 `<timeout>`，专项失败。
  原生记录及相关差分检查已完成，四板 STOP/释放/恢复原件均保留；未放宽判据。
  同源码有界复采 `delta-short-r2` 通过，运行零查询，全部 STOP 后核验分页/文件 CRC、
  FOLLOW 决策与实际模型，再释放采集池、恢复配置。

| 板卡 | MATCH / DECISION | 本轮实际更新 / 零调整 | DCO 序号 | 修正值变化 | 本地间隔全宽 |
|---|---|---|---|---|---|
| NO2 | 39 / 7 | 4 / 3 | 3→7 | −215→−860 ppb | 2001 ns |
| NO3 | 38 / 7 | 2 / 5 | 3→5 | −182→−385 ppb | 2001 ns |
| NO4 | 39 / 8 | 0 / 8 | 1→1 | 0→0 ppb | 2000 ns |

- 复采沿用首次采样后的真实 DCO，未重新假定从零开始。NO4 每个频差区间均跨零，
  不调整符合现有区间控制律，不能证明无需同步。末态 retained baseline/raw/rate/hz
  可独立重算候选；MATCH 降采样未必留存每个决策基线，不伪造全部决定的旧法反事实。
- 上一轮本地决策全宽约 5.5–9.8 µs，本轮约 2 µs；频差全宽由上一轮约
  8.3–14.1 kppb 变为本轮约 4.6–6.6 kppb。两轮启动、模型及输入不同，只作观测比较，
  不据此宣称物理漂移改善的因果结果。NO2/NO3 本轮全窗 residual 变化仍明确为正；
  NO4 的端点变化区间跨零。原生模型残差不等于实际输出锁相，100 ns 目标未完成。
- 证据根目录：`out/HardwareAcceptance/20260917/dpll-priority-delta-r1/`，包括
  `code-review.json`、`c11-review.json`、`resource-review.json`、
  `resource-independent-review.json`、`p3/`、两轮采样原件与日志；独立 host 原件在
  `out/HardwareAcceptance/20260917/typed-delta-host-r2/`。观测入口为
  `delta-short-r2/comparison.json` 和 `delta-short-r2/analysis/typed_residual_dco.svg`。
  独立 `hardware-review.json` 重算原生字节、CRC、区间及真实 DCO，并核验运行零查询、
  STOP/释放/恢复，结论为 `PASS_SCOPED_CORRELATED_LOCAL_DELTA_SLICE`。
- 下一 gate：继续 `VDC-FAST-003`，优先验证区间跨死区的 no_adjust 不立即丢弃同模型
  基线，在现有 `VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS` 内有界延长有效观测；模型采用、
  生命周期失效及到期仍须重新建立基线。只读端点分析支持这一更小候选，不代表新控制
  行为已实现或实板收益已证实。随后分解 NO1 逐帧 latch、bridge 和量化界，区分公共项
  与每次独立项。保留当前本地量化余量，不强迫非零调整；物理输出采用、相位、ACK、
  单圈期限与 VDC 正式发布仍分别验收。四板最终
  STOP，未操作 NO5、未修改 OTA；代码与文档分离提交，保留他人 flight engine 改动。

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

## 进度记录

（本节历史条目已按 C14 轮转，见文末 `## 归档索引`。）

## 验证与证据规则

每个 checkpoint 必须至少记录：

- 对应 TODO Task ID 和 owner；
- 当前源码/build/config identity；
- host/build/test 命令及结果；
- OTA/HIL/NO5/SD 原始证据路径；
- 失败原因、后继状态、回退点和下一 gate。

诊断 replay、host 单测、TDMA short-frame、NO5 外环观测和正式 VDC lock 是不同证据等级，
不得相互替代。formal promotion 失败时保留失败证据，不修改为成功状态。


## 归档索引

规则：此处只登记已迁出的历史证据段，条目正文逐字保留、未编辑。轮转规则见 §0 C14 / 环5。

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC-PROGRESS-20260916-043..VDC-PROGRESS-20260906-002 | 146 | 2026-09-17 |
