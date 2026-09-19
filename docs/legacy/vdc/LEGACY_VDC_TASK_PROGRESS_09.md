# VDC task progress archive 09

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_09.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-19

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
