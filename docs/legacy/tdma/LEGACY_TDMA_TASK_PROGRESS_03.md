# TDMA 基础件历史任务进度 03

Status: Frozen
Domain: TDMA
Canonical: `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_03.md`
Related: `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-09-19

> 本文件为历史证据归档，保留当时状态、失败及未编号旧 checkpoint；当前任务以主域 TODO 为准。

### TDMA-PROGRESS-20260913-067 - O6 相对游标定位取模

- 日期：2026-09-13
- 状态：PARTIAL；算术、生命周期和实板验证完成，完整峰值收益未证实，新增增长保留。
- 范围：`tdma_rx_scan_locate()` 依据 cursor 与 hint anchor 的相对距离对齐，
  短距离免除法、在 `UINT32_MAX` 范围内使用窄位宽取模，更大距离回退完整位宽。
  绝对 cursor、hint、完成字数、溢出保护和接收成功推进边界不变。当前路径已是
  增量游标，不把这次算术改写描述成从每拍全量扫描迁移为增量扫描。
- hint 与复制复核：继续保留 observation/request epoch、persona、配置、覆盖
  和坏头失效；STOP 保留取消 ACK。现有 ring 索引已用掩码、persona 在循环外
  选择位序、错位复制复用相邻字，不重复改写这些路径或删除复制后 DMA 复验。
- 软件快照（非事实源）：独立大整数 ceiling 模型执行真实定位函数，覆盖全部
  合法位相位、帧/尾边界、完成长度、窄/宽距离交界、绝对计数跨界、接近
  `UINT64_MAX`、空指针与无效 hint，以及固定种子的随机绝对计数；4 项通过。
  真实 RX/DMA 交错 28 项与完整 adapter 回归通过。首轮并行 pytest 共用默认
  basetemp，出现 10 项夹具文件丢失错误；改用独立目录后通过，`rx-tests-r1.log`
  原件保留，不作为固件错误或静默覆盖。
- 构建快照（非事实源）：A/B/Boot 通过，build `20260913154535`，源码指纹
  `814e12c7c49a5fe39856d4f2542e83c3030de3d46b8eda6c69740b4f23dcc3e1`，
  源文件数 1044。A/B 实际反汇编由两处 64 位除法调用减为一处大距离回退，
  短距离分支跳过除法，窄位宽路径使用原生 UDIV。A/B link-free 仍各 2200 B，
  扣除预留 heap 后额外余量各 152 B，无新增 RAM；PIO 产物字节一致。
- 硬件：当前源码 QUICK P3 的 strict_gates_passed=true，短帧与普通恢复通过。
  P3、两轮自主记录和普通恢复均完成 STOP/config ACK、许可证失效及 SD 逐字节
  核对；START 后零查询，使用 066 的四邮箱、固定矩阵与相同 v8 探针。
- 定位分项与完整外层（us，有限窗口快照，非事实源；每格 066→067；每行各取
  自己那一版该板的 ALL 峰值，不是独立分项最大值或跨板同一拍）：

  | 节点/轮次 | RX locate | ALL 外层 |
  |---|---:|---:|
  | NO2 R1 | 15.968→18.096 | 713.976→715.820 |
  | NO3 R1 | 20.260→20.324 | 695.712→778.248 |
  | NO4 R1 | 18.776→5.800 | 704.220→692.112 |
  | NO2 R2 | 23.320→19.904 | 711.248→711.868 |
  | NO3 R2 | 13.524→18.832 | 710.512→735.484 |
  | NO4 R2 | 21.320→21.820 | 696.256→685.252 |

  原始分项都有一次 RX 请求及一次定位，包含关系核对通过。指令减少未形成
  一致的定位或整环峰值收益，不能把 NO4 第一轮下降外推为固定 WCET 节省。
- 主站（R1/R2，us，有限窗口快照，非事实源）：RUN body 为 611.536/689.424，
  外层 646.920/714.560；STOP 外层 694.552/784.412。RUN 外层相对 066
  第一轮低 21.172 us、第二轮高 46.264 us，完整预算仍未达到。
- 新增增长归因：NO3 第一轮 body 增加 66.992 us，而定位仅增加 0.064 us；
  同拍 RX handoff 增加 28.088 us、overlay 增加 16.052 us、ring publish 增加
  11.104 us。主站第二轮 RUN 没有 locate 调用，RX_PARSE 从 151.324 增至
  238.544 us，其中 evidence/health/inspect 的增长分别为 37.492/34.036/
  20.108 us。主站第二轮 STOP 的 ring_runtime 增加 64.572 us。以上是不同
  保留峰值的分项对照，父子项不得相加；不能据此确认取模、缓存、总线或代码布局
  的因果。增长原件保留，后续先收敛这些峰值，不将局部算术验证写成性能验收通过。
- 自主两轮整段 passed/closed_loop/realtime=false，diagnostic=true。后段
  accepted 继续增长，四板 rejected/missing 无增长；全窗口 missing 最大仍为 1。
  periodic_interval/persona/adapter_tx/receive_missing 原失败保留。完整 500 us、
  正式 RAM、切换稳定性和逐圈特等席交付仍开放。
- 原件：`source-checkpoint-r1.json`、`disassembly-r1.json`、
  `cursor-comparison-r1.json`、`growth-attribution-r1.json`、
  `profile-comparison-r1.json` 及 `review-initial-r1.json`；已逐项重核 066 封存
  原件。按用户确认的四项顺序完成本轮借鉴，后续优先解决新增峰值与完整预算。

### TDMA-PROGRESS-20260913-066 - O2/O4 DMA 观察和 RX latch 分项

- 日期：2026-09-13
- 状态：PARTIAL；分项实现、回归与实板归因完成，完整预算和长期目标未达。
- 范围：在既有 DMA_OBSERVE 总项内拆出初始完成字数、已定位帧复制后复验和
  发现窗口复制后复验，保留每处 epoch/覆盖/回退检查；RX_LATCH 内拆出读取和
  重装，保留波形捕获占用、未 armed、空 FIFO、时间溢出和重装失败的原始行为。
  全部状态峰值和旧版本解码继续保留，不把新增诊断计作性能优化。
- 首轮归属修正：共享 latch helper 也被普通 origin TX completion 调用；首版
  无条件 RX 标签不满足父子关系，作为 superseded candidate 保留在
  `out/HardwareAcceptance/20260913/tdma-flight-dma-latch-timing/`，包含源码副本、
  build、P3、STOP 和 SD 原件。修正版只在 slave RX 路径记录这两个子项，并用
  实际共享 helper 的 master 情景验证硬件动作保留而 RX 标签为零，重新构建和验收。
  首轮 QUICK P3 的 strict_gates_passed=false，粗校准 NO1 的
  `TOPology 4,0,0` 应答超时；该拒绝与标签归属问题分别保留，不由后续成功覆盖。
- 软件快照（非事实源）：计时/非阻塞/latch 综合 47 项、RX/DMA 交错 28 项和
  完整 adapter 回归通过。实际生产代码验证两种复制后复验的次数与总项相符，
  覆盖后不得接受捕获；latch 捕获占用各状态均不消费 FIFO，不重装，原 PIO 顺序、
  时间戳、miss/count 和失败返回值继续保留。
- 构建快照（非事实源）：A/B/Boot 通过，build `20260913152450`，源码指纹
  `6a7a67db1e1a4803a7f358c8146f201e71a8d6e81cbe9716c5f783354015db75`，
  源文件数 1043。PIO 生成头与 065 字节一致。A/B link-free 各 2200 B，其中
  2048 B 为预留 heap，额外余量仅 152 B；较 065 净增 160 B，不代表正式 RAM 通过。
- 硬件：修正版当前源码 QUICK P3 的 strict_gates_passed=true，四板短帧及普通
  恢复通过；P3、两轮自主记录和恢复均完成 STOP/config ACK、许可证失效确认与
  SD 逐字节核对。START 后零查询，仍使用固定矩阵、四邮箱与相同板端记录窗口。
- 同拍分项（us，有限窗口快照，非事实源；每行取该板 ALL 峰值）：

  | 节点/轮次 | DMA 初始 | 帧复制后复验 | RX latch 读取 | RX latch 重装 | RX locate |
  |---|---:|---:|---:|---:|---:|
  | NO2 R1 | 12.300 | 6.736 | 11.736 | 22.684 | 15.968 |
  | NO3 R1 | 36.316 | 9.808 | 8.220 | 30.156 | 20.260 |
  | NO4 R1 | 15.708 | 9.392 | 21.780 | 19.876 | 18.776 |
  | NO2 R2 | 32.008 | 6.264 | 6.428 | 22.584 | 23.320 |
  | NO3 R2 | 28.032 | 9.460 | 13.264 | 17.440 | 13.524 |
  | NO4 R2 | 22.312 | 9.748 | 11.676 | 20.232 | 21.320 |

  各行初始观察、帧复验、RX 读取与重装各调用一次；发现窗口复验在这些保留峰值
  中未调用，不等于其全窗口成本为零。DMA 总项包含子项及记录成本，RX_LATCH
  包含读取和重装；原始调用数和包含关系已逐项核对。数据不支持把 latch 差异
  直接解释为超时重试，也不能把两个 DMA 观察点当作可删除的重复读取。
- 完整耗时（R1/R2，us，有限窗口快照，非事实源）：主站 RUN body 为
  640.008/631.364，外层 668.092/668.296；STOP 外层 686.840/696.296。
  follower ALL body 为 NO2 682.772/687.824、NO3 670.484/682.084、
  NO4 668.640/670.276。主站 RUN 外层较 065 高 30.516/8.908 us，但 v7/v8
  探针不同且主站峰值属于 RX 接受路径，不能把差值全归于新增 RX 探针或称为省时。
- 自主两轮整段 passed/closed_loop/realtime 仍为 false，diagnostic=true；后段
  accepted 增长，四板 rejected/missing 无增长，但全窗口 missing 最大仍为 1。
  periodic_interval/persona/adapter_tx/receive_missing 原失败保留，不外推逐圈稳定性。
- 原件：`source-checkpoint-r1.json`、`profile-comparison-r1.json`、
  `branch-attribution-r1.json`、`dma-latch-attribution-r1.json` 和首轮
  `attempt-manifest.json`；各自源码、构建与记录哈希纳入最终封存。
- 下一步：完成本切片证据闭环后进入 O6；不以减少复验或省略特等席证据来满足
  完整 500 us 和正式资源门禁。

### TDMA-PROGRESS-20260913-065 - O1 物理准入后提前复用无更新 TX

- 日期：2026-09-13
- 状态：PARTIAL；实现、正确性及实板对照完成，未证明完整峰值一致改善。
- 范围：异步 overlay 的 IDLE 路径先做已有物理 readiness 检查，处理 DMA selection
  退休并保留 armed/DMA/mode/role/persona/alignment 准入；无排队更新且原 map、
  epoch、local slot、TX generation/sequence 匹配时，跳过 inactive plan grant/configure。
  新描述符仍走完整准入与后台请求，未提供 readiness callback 的 backend 保留旧顺序。
  READY 提交、FAILED 处置、后台租约及 STOP/重新 ARM 的取消条件不变。
- 软件快照（非事实源）：真实物理 DMA 模型 4 项通过，覆盖连续自主计划、不同
  descriptor 相位发布、selection 退休、persona/role/mode/armed 拒绝、晚触发及
  有界 STOP；计时/非阻塞服务 33 项通过。完整 adapter 和 overlay 回归通过，位图
  所有权/对齐组合验证通过。新增 adapter 情景证明 pending selection 不计复用、
  PIO boundary 不自行退休、选中确认先于复用/新 grant、新描述符不能被快路径吞掉，
  REQUESTED/BUILDING/READY 的 STOP 取消与旧 epoch 拒绝继续通过。
- 构建快照（非事实源）：A/B/Boot 通过，build `20260913145049`，源码指纹
  `86b2132c464c685fe2f57c48ef926ccdf77a8fb19867b76c45153f1b5ac32b1d`。
  A/B link-free 均为 2360 B，扣除预留 heap 后额外余量仍为 312 B，PIO 生成头与
  064 字节一致。继续使用同版探针、四邮箱载荷、固定 057 矩阵与档案 ON。
- 当前源码四板 OTA、QUICK P3 流程及所执行严格门禁通过，短帧与最终普通恢复的
  passed/closed_loop/realtime/diagnostic 均为 true。两轮 START 后零查询、STOP 后
  导出与 SD 逐字节核对通过，最终四板 STOP、配置 ACK 和许可证失效均确认。
- 完整耗时快照（us，有限窗口，非事实源）：

  | 指标 | 064 R1 / R2 | 065 R1 / R2 |
  |---|---:|---:|
  | 主站 RUN body | 618.216 / 615.256 | 612.676 / 592.596 |
  | 主站 RUN 外层 | 634.632 / 645.780 | 637.576 / 659.388 |
  | 主站 STOP 外层 | 555.020 / 663.140 | 613.840 / 633.348 |
  | NO2 ALL body | 660.312 / 680.460 | 681.524 / 652.088 |
  | NO3 ALL body | 651.152 / 663.648 | 675.104 / 655.492 |
  | NO4 ALL body | 658.984 / 634.616 | 658.880 / 648.416 |

  当前 follower body 峰值的同拍外层分别为 NO2 703.944/685.660、NO3
  699.704/681.320、NO4 687.444/671.904 us。第二轮主站另有 ALL body 峰值
  615.268 us、同拍外层 630.812 us；它与按外层选中的 RUN 峰值不同，不能混拼。
  主站 RUN 外层高 2.944/13.608 us，follower 各轮有升有降，完整 500 us 仍未达到。
- 无更新机会快照（非事实源）：后段样本 18→25，NO2/NO3/NO4 的 TX 获取增量
  分别为 528/552/554 和 529/543/555，TX 复用增量仅为 55/18/1 和 43/10/0。
  `tx_reuse_count` 包含所有 FIFO 复用路径，只是本次提前复用次数的上界，不是
  快路径专属计数；尤其 NO4 此负载下几乎没有无更新机会。同期 TX 发布拒绝和 RX
  发布丢弃增量均为零，新 TX 与 overlay 提交持续推进。
  follower ALL body 峰值内 overlay 为 86.420/91.048/81.212 和
  81.180/76.288/112.388 us，不能把整段时间当作可删除 grant 成本或快路径收益。
  本切片证明可安全省掉无更新配置工作，未证明满更新负载的完整峰值获益。
- 两轮自主整段仍 passed/closed_loop/realtime=false、diagnostic=true；后段
  accepted 持续增长，rejected/missing 无增长，全窗口 missing 最大值各为 1。
  原失败保留。比较与原件引用见本根的 `overlay-comparison-r1.json`、
  `profile-comparison-r1.json`、`branch-attribution-r1.json` 和两轮 review。
- 下一步：进入 O2/O4，分开 DMA 初始观察/复制后复验和 RX latch 读取/重装；
  完整 500 us、逐圈特等席与正式 RAM
  要求继续独立验证。

### TDMA-PROGRESS-20260913-064 - O3/O5 请求交接与调度结果分支计时

- 日期：2026-09-13
- 状态：PARTIAL；O3/O5 分支归因、构建及实板证据完成，完整预算和长期目标未完成。
- 范围：按用户确认顺序先做 O3/O5；保留实际帧长、500 us 预算、既有 owner、
  后台租约、TX 时间戳读取/重装、任务调度和 STOP 决策。不将诊断当作性能优化。
- 实现：RX_REQUEST 包裹后台请求，记录 hint/参考证据、本地 TX 边沿取证和发布；
  边沿取证内区分 FIFO/时间换算与重装。READY 接受继续用 RX_PARSE，避免把
  handoff-capture 差值中的已知解析工作误报成未插桩。调度记录时间读取、任务绑定，
  select 分类为空队列、其他未派发、锁忙和实际派发，并记录周期刷新/过期清理。
  空队列结论来自锁内清理后的普通和恢复队列；原返回值、锁和判断顺序保留。
- 软件快照（非事实源）：记录器/解码/请求与调度分支/非阻塞服务及原 latch 算术
  综合回归 44 项通过；新增实际 TX latch 读取用例后的专项 12 项通过，包含空 FIFO、
  空指针、正常读回、时间溢出与重装失败。完整 adapter 回归通过。
  同一帧只在入场读取 TX latch，接受时不重读；空队列与门禁阻塞不混淆，父子区间
  包含关系、旧版本解码、延迟 RESET 和 STOP/运行代际归因继续验证。
- 当前构建快照（非事实源）：最终综合回归 45 项通过，A/B/Boot 构建通过，build
  `20260913142008`，源码指纹
  `b2d352223afe9f0b7ac9aa09d79d71c163f75606c9d86d0cf618fd7da7de12e6`。
  新增计时版本由 `TDMA_SERVICE_TIMING_VERSION` 定义；与 063 探针集不同，完整
  phase 不扣除新探针成本，不作为同条件优化收益对照。PIO 生成头与 063 字节一致。
- 构建失败快照（非事实源）：首次新增分项后 RAM 超出 128 B；原 063 link-free
  的 2440 B 包含链接器 `.heap` 预留 2048 B，额外余量实际为 392 B，不应解释为
  全部可新增静态数据。失败原日志保留。调用计数改为单 phase 紧凑存储并增加饱和
  无效标记，保留全部分项和状态峰值；修正后 A/B link-free 为 2360 B，扣除预留
  heap 后额外余量为 312 B。本次净增 80 B，不代表正式 RAM 门禁通过。
- 当前源码四板 OTA、QUICK P3 流程与所执行严格门禁通过；短帧与最终普通恢复的
  passed/closed_loop/realtime/diagnostic 均为 true。本轮未重现 063 粗校准拓扑超时。
  固定 057 矩阵、四邮箱、档案 ON，两轮 START 后零查询、STOP 后导出，SD 逐字节
  核对通过。最终四板 STOP、配置 ACK 和许可证失效均确认。
- 完整耗时快照（us，有限窗口，非事实源）：

  | 指标 | R1 | R2 |
  |---|---:|---:|
  | 主站 RUN body | 618.216 | 615.256 |
  | 主站 RUN 外层 | 634.632 | 645.780 |
  | 主站 STOP 外层 | 555.020 | 663.140 |
  | NO2 ALL body / 同拍外层 | 660.312 / 685.240 | 680.460 / 716.780 |
  | NO3 ALL body / 同拍外层 | 651.152 / 676.424 | 663.648 / 692.024 |
  | NO4 ALL body / 同拍外层 | 658.984 / 680.356 | 634.616 / 655.524 |

  NO4 第二轮另有按外层选中的 OTHER 峰值 656.004 us，完整原记录保留。主站 RUN
  外层较 063 高 13.548/23.760 us，探针集和记录布局变化，不能把差值全部归给
  新增探针，也不能据此宣称性能改善。第二轮主站 ALL 峰值是 STOP 1280→0，
  不能用其 RX/adapter 零值描述自主 RUN。
- O3/O5 归因快照（us，follower ALL body 峰值各自同拍分项，非事实源）：

  | 节点/轮次 | 请求交接 | 本地 TX 取证 | TX 读取 | TX 重装 | 空队列 select | 其中刷新/清理 |
  |---|---:|---:|---:|---:|---:|---:|
  | NO2 R1 | 114.448 | 61.476 | 7.808 | 29.532 | 36.260 | 19.752 |
  | NO3 R1 | 84.152 | 42.212 | 5.076 | 20.688 | 37.272 | 14.344 |
  | NO4 R1 | 77.448 | 29.468 | 3.252 | 9.940 | 40.980 | 18.020 |
  | NO2 R2 | 80.472 | 49.644 | 4.108 | 25.940 | 37.156 | 18.016 |
  | NO3 R2 | 114.020 | 62.528 | 7.528 | 33.056 | 33.996 | 14.324 |
  | NO4 R2 | 110.184 | 78.868 | 11.456 | 41.280 | 30.068 | 18.400 |

  请求是 RX_HANDOFF 的子项，本地 TX 取证是请求子项，读取和重装又是取证子项；
  select 包含刷新/清理，均不得重复累加。本轮 follower 峰值均为请求拍，扣除
  capture 与 request 后交接剩余仅 9.404–16.656 us，包含分支和探针成本。
  主站 RUN 峰值为接受拍，RX_PARSE 为 232.236/205.700 us，本地 TX 取证未执行；
  select 为 32.892/32.524 us，仍为空队列。主站 STOP 则为 SELECT_BLOCKED
  5.332/8.860 us、无刷新，不能混入空队列统计。
  本地 TX 重装高于 FIFO 读取，但取证还包含关联/包装开销，不能把整个交接归因给
  重装。保留峰值不能推导分支出现频率或删除该阶段的收益。
- 两轮自主整段 passed/closed_loop/realtime=false、diagnostic=true；后段四板
  accepted 持续增长、rejected/missing 无增长，全窗口 missing 最大值仍各为 1。
  原 periodic_interval、persona、adapter_tx 和 receive_missing 拒绝保留；当前
  源码短帧闭环通过不等于自主切换整段、逐圈特等席或完整 500 us 已验收。
  原件见 `branch-attribution-r1.json`、`branch-stages-r1.csv`、两轮 review 与
  `profile-comparison-r1.json`，均位于本切片证据根。
- 下一步：执行 O1，提前复用仍先确认 DMA selection 退休和物理准入，再跳过无更新
  的配置准备，保留新数据准入、map/epoch 校验及 STOP 取消；DMA 初始/复制后复验、RX latch
  分解与游标优化按确认顺序继续，特等逐圈交付和正式资源/WCET 门禁不省略。

### TDMA-PROGRESS-20260913-063 - STOP 后按拓扑选择邮箱数量并在 ARM 冻结布局

- 日期：2026-09-13
- 状态：PARTIAL；运行时布局实现、软件边界回归、构建与四节点实板对照完成，
  完整预算、严格校准及全拓扑验收未完成。
- 范围：复用已有 `SYSTem:TDMA:RING:TOPology`，按 staged topology 的节点数生成
  产品 map；不增加独立邮箱数，不改变静态容量、单邮箱内容、1 kHz 或完整 phase 预算。
  节点数必须适配全环 profile、slot、reference 和 Calibration 配置，旧校准不能跨新拓扑
  直接准入。物理 ARM 复核 map 长度与 config 节点数相符，RUN 内长度固定。
- 实现：静态数组仍由 `PROJECT_NODE_CAPACITY` 限定；紧凑 TX layout 携带 payload 长度，
  后台 overlay 使用冻结的包长和物理配置，origin DMA 构图与 exchange 显式绑定包长，
  trailer 使用实际 payload 尾部；超容量、异长度、越界 active mask 在读取或发布前拒绝。
  原有本地 mailbox/完整前缀 TX 视图继续只授权本节点，不扩展 writer 边界。STOP 的后台
  取消 ACK、epoch、generation 和 DMA 退休要求保留，拒绝旧准备结果进入新布局。
- 软件快照（非事实源）：同一容量下的 4→5→6→4 map 激活、overlay 构建、运行中
  配置拒绝、旧 generation 拒绝及取消租约回归通过；实际 DMA 图逐节点长度、trailer
  地址、复制末尾 canary 与异长度 receive-health 拒绝通过；当前四/五/六邮箱与八容量
  对照的实际 PIO 模型 216 项通过，C 位图/权限/非字节对齐穷举 122880 例通过。
  ARM map 工厂直接从生产函数构建测试，验证允许范围及每个 segment 的真实布局。
  初始 host 夹具缺少新增物理 payload 长度、紧凑 layout 长度和 profile 常量 include 的
  错误均保留并修复；原 partial TX 输入行为保留，时钟测试显式配置产品 map。
- 构建快照（非事实源）：容量/预算/bitmap/origin/DMA 综合回归 61 项通过，A/B/Boot
  构建通过，build `20260913132754`，源码指纹
  `8fa59d764de603e6ec7f4b4d111edca5fdce591cb4ce6a18fe4cb072271a0f69`。
  编译容量为六节点，四/五/六节点 payload 分别为 132/164/196 B，transport packet
  分别为 164/196/228 B。A/B link-free 均为 2440 B，比 062 少 8 B；运行时减少节点
  不释放预留静态池。PIO 生成头与 062 逐字节一致，资源归属不变。
- 硬件快照（有限窗口，非事实源）：当前源码四板 OTA 成功，QUICK P3 流程完成，
  process-image 的 passed/closed_loop/realtime/diagnostic 均为 true。粗校准阶段
  NO3 的 `TOPology 4,2,2` 应答超时，strict_gates_passed=false；保留原件，不能
  宣称严格全绿。沿用固定 057 矩阵、档案 ON，两轮对照如下；062 为相同四板环内
  节点但固定六邮箱，063 按四节点拓扑使用四邮箱。各数值为有限窗口保留峰值，
  不是固定 WCET 节省量，嵌套计时不得相加。

  | 指标（us） | 062 六邮箱 R1 / R2 | 063 四邮箱 R1 / R2 |
  |---|---:|---:|
  | 主站 RUN body | 606.628 / 605.028 | 591.748 / 596.876 |
  | 主站 RUN 外层 | 634.448 / 627.672 | 621.084 / 622.020 |
  | 主站 STOP 外层 | 478.988 / 543.060 | 593.480 / 536.524 |
  | 主站 ALL body | 611.008 / 605.028 | 591.748 / 596.876 |
  | NO2 ALL body | 649.208 / 651.732 | 648.760 / 668.592 |
  | NO3 ALL body | 659.992 / 654.460 | 630.712 / 639.144 |
  | NO4 ALL body | 652.548 / 624.996 | 604.944 / 610.420 |

  四板 map payload 和已接受 payload 均为 132 B，物理 byte count 为 173，比 062
  减少 64 B。主站 RUN 外层分别下降 13.364/5.652 us，但首轮 STOP 外层上升
  114.492 us、第二轮 NO2 ALL body 上升，不能宣称完整 phase 或整环一致改善。
  两轮后段各板 accepted 持续增长，rejected/missing 无增长；全窗口 missing 最大值
  各板仍为 1。自主整段 passed/closed_loop/realtime=false、diagnostic=true，
  原 periodic_interval、persona、adapter_tx 与 receive_missing 失败保留。
  两轮 START 后零查询，板端记录完整、STOP 后导出，SD 副本逐字节核对通过。
  最后普通恢复全部判据通过，四板 STOP、配置 ACK 和许可证失效均确认。
- 后续：五、六个实际环内节点需要对应实环验收；host 布局切换不能代替缺失硬件拓扑，
  完整 500 us、正式 RAM、协调启停与长稳仍按原目标继续。

### TDMA-PROGRESS-20260913-062 - 编译容量邮箱与运行时节点切换前置验证

- 日期：2026-09-13
- 状态：PARTIAL；编译容量邮箱实现、实板对照与普通恢复完成，完整目标未通过。
- 用户授权与范围：先将产品邮箱数量与 `PROJECT_NODE_CAPACITY` 对齐，再接入 STOP 后
  配置节点数、重新 ARM 生效；用户明确后续有四、五、六邮箱版本。该指令覆盖此前
  邮箱工作后置与固定八邮箱限制，单邮箱内容、DPLL 语义、owner 和完整预算不变。
- 当前实现：产品包长由 `TDMA_FLIGHT_SHORT_PACKET_SIZE` 推导，origin capture、RX copy、
  stage、DMA 捕获计数与 padding 使用实际包长；overlay 工位和 owner 配对使用同一长度。
  超容量 active mask 在构图前拒绝；通用 transport 上限及 RefMem/Calibration 存储 ABI 保留。
  `TDMA-FLIGHTBITMAP-01` 修订版本，状态仍为 pending，不自批独立审核。
- 软件与构建快照（非事实源）：容量/预算/bitmap/origin/DMA 回归 48 项通过，实际 PIO
  模型 108 项通过；完整 adapter 与 map host 通过，CRC 保留全部状态/字节穷举。
  初始 host 失败记录保留：旧包长可进入缩短位图而越界，overlay 工位仍按旧 sizeof
  处理，以及旧 owner 配对条件未改；均已修复并回归，未在硬件运行失败版本。
  既有 STOP 夹具补齐真实 build-job 取消依赖，未改变生产 STOP 流程。
  build `20260913125033`，源码指纹
  `20b31c39c04e3c674b1016fd7cb02f51aa127c6ee86ec6423335e563e0ce31cf`；A/B/Boot 构建通过。
  六邮箱 payload 为 196 B、transport packet 为 228 B；相对旧八邮箱 packet 减少 64 B。
  A/B link-free 均从 2312 B 增至 2448 B，增加 136 B；不是正式运行时 RAM 门禁通过。
  生成 PIO 头与前序构建逐字节一致，无新增 SM 或 DMA 资源。
- 硬件快照（有限窗口，非事实源）：当前源码四板 OTA 成功，P3 process-image 的
  passed/closed_loop/realtime/diagnostic 均为 true；但粗校准阶段主站 topology 命令
  应答超时，P3 流程有界继续且 strict_gates_passed=false，原始失败保留，不能写成严格全绿。
  固定 057 矩阵、档案 ON 的两轮自主记录如下；前序为 060 构建，同计时版本、相同配置。

  | 指标（us） | 旧八邮箱 R1 / R2 | 六邮箱 R1 / R2 |
  |---|---:|---:|
  | 主站 RUN body | 639.188 / 656.208 | 606.628 / 605.028 |
  | 主站 RUN 外层 | 668.796 / 675.156 | 634.448 / 627.672 |
  | 主站 STOP 外层 | 795.472 / 696.516 | 478.988 / 543.060 |

  四板记录的 map payload 和已接受 payload 均为 196 B，物理 byte count 为 237；
  PIO 周期仍由既有 cadence/guard 保持，不能将减小传输区等同于降低完整循环周期。
  两轮后段各板 accepted 持续增长，rejected/missing 均无增长；全窗口各板 missing
  最大值仍为 1。自主切换整段 passed/closed_loop/realtime=false、diagnostic=true，
  不提升为产品稳定性通过。follower ALL body 峰值约 625–660 us，不能混用主站 RUN
  状态分类宣称整环达标。各轮 START 后零查询，板端记录完整、STOP 后导出，SD 保存
  逐字节核对通过。最后普通恢复全部判据通过，四板 STOP 确认、许可证失效。
- 下一步：完整 RUN、STOP、ALL 分别比较；完成本切片后再使 ARM 从已准入 topology
  生成运行时 map、DPLL trailer 与 DMA 长度。静态池以编译容量为上限，RUN 不改变帧长，
  旧后台任务、generation、epoch 和不同长度不得跨配置复用。

### TDMA-PROGRESS-20260913-061 - TDMA 协调启停候选与本地停止调用审查

- 任务：`TDMA-FLIGHT-002B/002F`。日期：2026-09-13。状态：`PARTIAL`。本轮为设计与
  现有源码审查，不冻结契约、不修改 registry 状态；以下阶段名为候选语义，未实现。
  基线提交 `20592bde233e6e2c84567ec425e807207811a30d`，证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-coordinated-lifecycle-review/`。
- 可行性：TDMA 可以在最小通路建立后承载生命周期意图和确认。HAOFV 仍由
  `TdmaSchedulerAO`/Core1 owner 管生命周期，FB/ECC 分步推进，Vector 发布事实；
  Core0 只做授权准备与命令交接，VDC 提供合格共同时间，PIO/DMA 执行预授权边界
  动作。SCPI 只触发，板端记录，停止后统一导出。分阶段增加准备或停止总延迟，
  目的是压低单次 action 峰值并约束生效边界，不能直接宣称完整 phase 达标。
- 候选执行阶段：

  | 阶段 | 处理与退出条件 |
  |---|---|
  | 本地最小通路 | 本地 owner 完成资源准入并建立接收/转发和必要基础发射；完全关断的环不能接收自身 START。冷启动不能先要求依赖该环取得的 VDC lock。 |
  | PREPARE / READY / NACK | 各节点准备固定工位、布局和计划；READY 绑定真实资源、配置与事务。准备期间保留已准入的旧通路；共享 union 必须停止旧 DMA 的现有限制仍有效。 |
  | 分发执行目标 / COMMIT | 提前分发目标圈或 VDC 共同时间并验证必要参与者就绪、目标仍在未来和截止期限。目标改变必须使旧确认失效；消息迟到不能改成收到即执行。 |
  | 边界执行 | Core1 事先安装完整计划，硬件到达授权边界只做短动作；实际执行记录绑定事务与边界，协调方收齐实际事实后才能发布完整 RUN。 |
  | STOP_PREPARE / DRAIN | 先关闭新业务装载，保留转发、生命周期通知及最后有效时间证据；等待已接收业务与末圈退休，排空不能无限等待。 |
  | 关断 / 回收 | 在链路尚通时完成必要确认及最终关断安排，再按预装末圈边界或共同时间停线；后台取消 ACK、DMA 退休和资源回收可有界分拍，全部完成后才确认本地 STOP。 |

- 通知与业务载荷：`TDMA_PROCESS_IMAGE_CONTROL_SIZE` 当前仅容纳 opcode/seq8。
  `distributed_refmem.c` 的发送路径写 `NONE` 和空序号，接收路径只保存
  `last_control_opcode/last_control_seq8`；不存在上述握手。候选利用已注册的固定
  命令/事实 slice 提前传完整事务，再由短控制字段引用，但具体编码、分片、静态
  配额和 owner 须先评审。事务至少绑定发起者、生命周期 epoch、完整 generation、
  配置标识、参与节点集合、操作及执行目标；短序号回绕、重复、旧命令和重启均需
  拒绝或幂等处理，不能把现有 RefMem ACK 直接解释成 TDMA READY。生命周期控制
  应有独立固定服务保证，不能与日志共用可丢弃队列；VDC 时间戳保全仍是独立要求。
  本候选不假定有空闲 wire、PIO 指令、DMA 或新增 RAM 可用。
- 精度：收到即执行会叠加逐跳传播、接收解析和 Core1 调度延迟。目标圈只约束逻辑
  次序，各节点到达该圈的物理时刻仍不同；物理同时执行还需要合格 VDC、局部硬件
  时间映射和边界触发。误差预算须分列共同时间误差、映射误差、硬件量化和触发
  抖动；目标提前量覆盖控制传输/确认上界、最慢准备者与安装余量，不能只取物理
  一圈时间。最小通路先建立再收敛 VDC，避免冷启动依赖闭环。
- 丢失与关闭：READY/COMMIT 丢失、配置改变、迟到或 NACK 必须保留拒绝事实并按
  截止期限处理。链路关闭后不能再依赖该链路收 STOPPED 确认；最终关断安排预先
  分发，关断后的本地完成事实由后续诊断读取。网络在提交中断裂时可能出现部分
  节点已执行，消息握手本身不保证故障下原子切换；本地 watchdog/截止期限及
  epoch 失效承担恢复，协调方不得发布全环成功。不可恢复硬件故障保留本地立即
  停止路径，不等待网络排空。正常分段关闭不应制造中间节点提前断转发的失联。
- 本地 STOP 审查：`tdma_pio_spi_phys_disarm()` 首次调用
  `tdma_pio_spi_phys_stop_command_dma()`，末尾的
  `tdma_pio_spi_phys_release_flight_resources()` 再调用同一函数。后者还服务多个
  ARM 失败路径，不能全局去掉停止检查；`armed=false` 也不证明 DMA 已退休。
  program manager 会复验 overlay 活动与 DMA quiesced，再释放实际 persona 资源。
  首次退休与释放之间的 waveform restore 使用 `rearm=false`，但完整调用关系、
  所有权、失败重试仍须通过真实函数执行验证，才可复用同次 owner 调用的退休结果。
  跨拍清理还须审查 adapter ARM 回滚中忽略 disarm 返回值的入口，以及返回
  `disarm && select_persona` 的 maintenance 入口，不能直接使现有同步回滚提前完成。
- 耗时归因仍受原数据限制：`060/stop-review-r1.json` 的 ring_runtime 子项包含
  adapter、物理停止和后续清理，未分解到单次 DMA 停止。共享 deadline 只约束该次
  DMA 轮询，不覆盖 GPIO、程序恢复、快照与数组清理；重复调用结构早已存在，
  不能据它解释全部新增长，嵌套计时也不能相加。未新增硬件采样，旧 RUN/STOP
  原始峰值及失败继续保留，本轮没有新的 WCET 收益声明。
- 验证：现有 command-DMA、origin record/build-job、service nonblocking 和 origin
  admission pytest 共 22 项通过、1 项失败（本次测试快照，非事实源）。失败项
  `test_descriptor_completion_and_bounded_stop` 的生成夹具未声明
  `s_tdma_origin_build_job`，也未接入 `tdma_origin_build_job_cancel`；对原生成 C
  单独编译复现相同错误，见 `lifecycle-tests-r1.log` 与 `dma-fixture-compiler-r1.log`。
  三层晚写/共享 deadline、记录冻结、后台取消等其他用例通过，不替代失败用例。
  源码锚点、前序证据 SHA-256 与本轮文档门禁归档见 `source-audit-r1.json` 和
  `review-final-r1.json`。仅文档提交，无新 build、OTA、P3 或网络协议验收声明。
- 下一 gate：先补齐该测试夹具依赖，再进行最小 STOP 退休/清理切片；覆盖取消未
  ACK、DMA 超时与晚写、失败 ARM、重复 STOP、maintenance 切换、资源保留及重用。
  实现改动后必须重新构建、真实 P3 和固定矩阵板端取证，分别核对 RUN/STOP/ALL
  完整 phase。该门禁之后再进入网络编码及协议实现；完整 500 us、切换稳定性、
  service blackout、特等席逐圈交付与正式 RAM 仍开放，邮箱容量调整保持后置。

### TDMA-PROGRESS-20260913-060 - mailbox CRC 逐字节等价运算与 STOP 峰值增长留证

- 任务：`TDMA-FLIGHT-002B/002F`。日期：2026-09-13。状态：`PARTIAL`。继续降低 Core1
  完整执行成本；HAOFV 唯一 owner、静态调度和 500 us 目标保持。
  证据根：`out/HardwareAcceptance/20260913/tdma-flight-mailbox-crc-byte/`。
  以下数字均为当前构建、离线测试或有限窗口快照，非事实源。
- 实施：`tdma_process_image_crc16_ccitt()` 原来逐字节执行多次位递推，现由
  `tdma_process_image_crc16_update_byte()` 以移位和异或等价折叠。多项式、初始值、
  输入顺序、余数截断和 NULL/空输入行为保持，无查表、附加存储或新资源。
  Core0 mailbox 编码/解析及 Core1 adapter、物理 shadow 发布的全部既有 CRC
  检查仍执行；没有以准备接受或 header CRC 替代 mailbox 完整性。
- 软件验证：实际公共字节更新函数对全部 65,536 个 CRC 状态 × 256 个字节，即
  16,777,216 次转移，与独立保留的逐位参考递推逐项比较通过。CCITT-FALSE 标准向量、
  NULL/空输入及零、全一、递增/递减和伪随机数据的 4,104 个前缀通过；由相同初态和
  全转移等价保证任意字节流与旧算法兼容。原 map 与完整 ring-adapter host 测试通过，
  包括上一切片的错误 CRC/source/owner、物理拒绝重试等用例。测试沿用仓库原生
  PowerShell runner，完整命令记录在 `crc-tests-command-r1.json` 与
  `adapter-tests-command-r1.json`；文档检查器 18 项测试通过。
- A/B/Boot 构建 `20260913114847`，源码指纹
  `334bb7f7faf1521615270571c96727f7c88af2f66529a50b14bf7b9d6d05b166`，
  1,040 个源码文件；六节点编译、固定 wire 和 PIO/DMA/SM 资源保持。A/B link-free
  均仍为 2,312 B，生成 PIO 头与 059 逐字节相同，正式 RAM 门禁未据此通过。
- 实际链接 A/B 的 adapter mailbox validator 与物理 origin publish 中，CRC 每字节
  循环均从 44 个指令位置缩为 9 个，旧的八组条件异或已替换，最终存储 CRC 比较仍在。
  两函数代码分别从 170/268 B 缩为 94/188 B，见 `crc-codegen-r1.json`；指令数
  不等于时钟周期或完整 WCET，不能直接按比例推算 phase 收益。
- 固定 057 的 `p3-r1/trn03-matrix.json`、档案 ON、板端 `250000 us × 26`，保持
  START 后固定延迟授权及 RESET，运行中零查询，STOP 后导出。比较保留同条 RUN
  body/外层及全部状态峰值，核对两端 state/config/非零 epoch 与 RESET generation。

  | 固件 | RUN body 两轮 us | RUN 外层两轮 us | STOP 外层两轮 us |
  |---|---|---|---|
  | 059 | 657.276 / 633.520 | 697.100 / 658.300 | 471.308 / 596.672 |
  | 060 | 639.188 / 656.208 | 668.796 / 675.156 | 795.472 / 696.516 |

- 第一轮 RUN 外层较旧同轮次低 28.304 us，第二轮高 16.856 us；不能宣称完整 phase
  一致改善。两轮新 STOP 均高于旧两轮保全峰值；第一轮 ALL body 的 765.224 us
  来自 STOP，同条外层为 795.472 us，不能并入 RUN 或用较低 RUN 值覆盖。
  `stop-review-r1.json` 确认两端状态为禁用自主 owner 到停止态，同一 config/epoch；
  第一轮 ring_runtime 子项为 644.468 us。停止源码未修改，A/B disarm 去除地址后的
  指令序列与旧版相同，但这不足以把增幅归因到缓存、总线或 CRC。既有 disarm 直接
  停 DMA，后续 release_flight_resources 又进入停 DMA；该调用结构早于本切片，
  是否可安全分段或复用已完成退休事实，须保持取消 ACK、故障重试和资源所有权验证。
- 两轮 startup barrier 均通过，原整段 passed/closed_loop/realtime 均为 false，
  diagnostic=true、error 为空。第一轮仅 NO2 记录一次 missing；第二轮 NO1/NO2
  各一次，其他节点为零。后段主站 accepted 增量为 852/850，四板后段 reject/missing
  均不再增长。原 `receive_missing_grew`、`adapter_tx_not_growing` 和
  `physical_flight_persona_mismatch` 等失败保留；映像陈旧计数没有被重置或屏蔽，
  有限窗口较少 missing 不构成切换无损或逐圈可靠性证明。
- 当前源码真实四板 OTA/P3 完成，QUICK 范围 strict_gates_passed=true、failures
  为空，普通短帧与最终固定矩阵普通恢复均通过。P3、两轮自主窗口和普通恢复全部
  完成四板 STOP/config ACK、许可证失效、SD 保存及逐字节读回。源码、build、P3
  与原始数据绑定见 `source-checkpoint-r1.json`、`profile-comparison-r1.json`、
  `review-final-r1.json`；分离提交与封存索引见 `commit-proof.json`、`slice-manifest.json`。
- 下一 gate：优先拆分 STOP 的 DMA 退休、GPIO/程序/资源回收及 adapter 清理成本，
  继续收敛 RX/owner 完整开销。CRC 等价与局部指令减少已验证，完整 500 us、STOP
  峰值增长、自主切换、service blackout、特等席逐圈交付及正式 RAM 均继续开放。
  TDMA 协调启停协议与邮箱容量后续项保持原顺序；registry 状态未改，长期目标继续。

### TDMA-PROGRESS-20260913-059 - 自主 origin 直接装载已准备的固定本地邮箱

- 任务：`TDMA-FLIGHT-002B/002F`。日期：2026-09-13。状态：`PARTIAL`。继续处理运行期
  时间增长及剩余 Core1 成本；HAOFV 唯一 owner、静态调度和 500 us 目标保持。
  证据根：`out/HardwareAcceptance/20260913/tdma-flight-origin-mailbox-load/`。
  以下数字均为当前构建、离线测试或有限窗口快照，非事实源。
- 归因：原 `tdma_pio_spi_ring_origin_publish()` 对每个新 TX 版本调用通用
  `tdma_flight_engine_tx_load()`，复制完整 map/过程映像、遍历 segment 并构造逐字节
  输出位图，最后只向自主物理图提交本地邮箱。激活时已有整 map 检查与固定位置授权，
  FIFO active 版本也已由 Core0 完整发布，允许直接选择受授权的本地字节。
- 实施：`tdma_flight_engine_copy_tx_layout()` 单次读取非零整邮箱授权，核对 slot、
  长度、owner mask、mailbox 头/CRC，再由 `tdma_flight_engine_accept_tx()` 复验
  active/map generation 并记录准备接受。紧凑、整映像及有效前缀输入都只选本地位置；
  物理 shadow 同步复制、完整 owner generation/sequence、物理失败后的延后重试和
  成功前不更新映射保持。准备接受不等于 SENT/ACK，不授权写远端邮箱或跳过 RX 检查。
- 软件验证：完整 ring-adapter host 单测通过，新增所有固定槽位与紧凑/整映像/前缀、
  CRC/source/长度/owner 错误、map writer busy、inactive、非整邮箱 map、slot 不一致、
  物理拒绝重试和版本复用覆盖，共 8 槽 × 12 场景；原 bootstrap/prepare/STOP 用例仍
  执行。service_nonblocking/origin_admission 共 16 项、文档检查器 18 项通过。
- A/B/Boot 构建 `20260913111853`，源码指纹
  `7009a3ac691ea71e405beb76ad8dce692e4b0feb88219414cf8f05f35d102900`，
  1,040 个源码文件；六节点编译、固定 wire、PIO 与 DMA/SM 资源保持，A/B link-free
  仍为 2,312 B。生成 PIO 头与 058 逐字节相同，正式 RAM/运行栈门禁未据此通过。
  A/B 反汇编确认旧 origin publish 的通用 tx_load 调用已由固定授权读取/接受取代。
  首轮 callsite helper 错认旧调用被内联而失败；R2 按旧独立函数和新内联位置纠正，
  两版原件均保留。调用落点证明不等于 WCET 证明。
- 固定 057 的 `p3-r1/trn03-matrix.json`、档案 ON、板端 `250000 us × 26`，START
  后固定延迟授权及 RESET，运行中零查询，STOP 后导出。`profile-comparison-r1.json`
  绑定两版完整 RUN 峰值原件，复核两端 state/config/非零 trial epoch 一致与 RESET
  generation；每行 body 与外层来自同条保全记录。

  | 固件 | RUN body 两轮 us | RUN 外层两轮 us |
  |---|---|---|
  | 058 | 713.720 / 698.812 | 744.960 / 728.164 |
  | 059 | 657.276 / 633.520 | 697.100 / 658.300 |

- 同轮次保全峰值差额为 47.860/69.864 us，支持本切片有限窗口改善，不能作为固定
  WCET 节省。外层仍不包含分类后置发布；完整 500 us 继续未通过。新两轮同条峰值中
  owner_service 为 595.484/576.576 us，其内部 RX handoff 为 224.948/239.416 us；
  嵌套分项不能相加，后续继续拆分接收接受/提交与 owner 固定成本。
- 两轮 startup barrier 均通过，整段 passed/closed_loop/realtime 均为 false，
  diagnostic=true、error 为空。原 periodic interval 失败包含
  `receive_missing_grew`、`adapter_tx_not_growing` 与
  `physical_flight_persona_mismatch`，未重写原模式门禁。第一轮 NO1/NO2/NO4 各记录
  一次 missing，NO3 为零；第二轮四板各一次。该计数来自
  `tdma_receive_health_observe_missing()` 的接收映像陈旧期限，不是直接物理丢帧
  计数，本轮未清零或屏蔽。两轮后段主站 accepted 增量为 851/849，四板后段
  reject/missing 均不再增长；这不取消切换失败，也不能证明线路无缺口。
- 当前源码真实四板 OTA/P3 完成，QUICK 范围的 `strict_gates_passed=true`、failures
  为空，普通短帧 passed/closed_loop/realtime/diagnostic 均为 true；不提升为自主
  或完整实时验收。最终固定矩阵普通恢复通过。P3、两轮自主长窗及普通恢复均完成
  四板 STOP/config ACK、许可证失效和 SD 逐字节读回。源码/build/P3 复核见
  `source-checkpoint-r1.json`、`review-final-r1.json`；分离提交与封存索引见
  `commit-proof.json`、`slice-manifest.json`。
- 下一 gate：继续降低完整 Core1 成本，并分开测量真实停线边沿、首返回帧和后台接收
  延迟。TDMA PREPARE/READY/NACK、生效圈和有界排空仍是待评审协议，不能用当前
  opcode/seq8 字段宣称已支持；冷启动本地接收/转发前置，同圈与 VDC 共同时间下同时
  执行分别验收。完整自主稳定性、特等席逐圈交付、service blackout、正式 RAM 与
  邮箱容量后续项仍未闭合；registry 状态保持，长期目标继续进行。

### TDMA-PROGRESS-20260913-058 - 纯 origin 构图移出 Core1 准备步进与取消交接

- 任务：`TDMA-FLIGHT-002B/002F`。日期：2026-09-13。状态：`PARTIAL`。响应用户通过
  TDMA 协调逐步启停的方向，先完成本地准备空窗切片；HAOFV 唯一 owner、静态调度与
  500 us 目标保持，未改 wire、PIO 程序、DMA/SM 分配或编译节点容量。
- 证据根：`out/HardwareAcceptance/20260913/tdma-flight-lifecycle-preparation/`。
  以下数字均为当前构建、离线测试或有限窗口快照，非事实源。
- 归因：旧 `tdma_pio_spi_phys_origin_poll()` 先停旧 DMA，再每次 owner poll 构建一个
  graph block，两遍完成才允许 seed/安装。origin runs 与活动 RX ring 共享 union，
  Calibration 缓冲还持有独立完成读回，不能在运行时直接覆盖或借用这些存储。
- 实施：`tdma_origin_build_job` 位于 union 外，由 Core1 初始化 builder 并发布借用；
  Core0 既有准备服务完成固定 label catalog 两遍纯构图，逐块检查取消，不访问 MMIO。
  READY release 发布完整图，Core1 单次 acquire 接收后继续 seed/配置/安装，动态授权、
  配置/时钟/资源复验保留。STOP 即使已停硬件，也等活动后台写者最后写入并确认退休，
  再释放 workspace；旧结果不能进入新请求。没有新增实时任务或运行期逐圈构图。
- 软件验证：18 项初始回归、33 项 job/档案/资源回归和完整 ring-adapter host 单测通过。
  覆盖 claim 前后、块内、最后发布、READY、失败与步骤上限的取消和复用；实际 STOP
  函数在后台未退出时拒绝提前冻结/释放。真实 builder 对 2–8 节点及档案 ON/OFF 共
  14 组图逐字节比对，descriptor、literal 和入口一致。文档检查器 18 项测试通过。
- A/B/Boot 构建 `20260913105135`，源码指纹
  `077a0a418f6a63002fdeb006bcd7f0994db3c7b8179609ea0b0f8841b0757bf3`，
  1,040 个源码文件，含新增文件。A/B link-free 从 2,320 B 到 2,312 B，交接状态占用
  8 B；生成 PIO 头与 057 最终构建逐字节相同。正式 RAM/运行栈门禁未据此通过。
- 切换对照：固定 057 的 `p3-r1/trn03-matrix.json`，板端 `20000 us × 26`；SCPI
  只触发，运行中零查询，STOP 后导出。以 owner 准备态所在样本的 started/completed
  区间给出保守边界；这不是逻辑分析仪测得的真实停线边沿。

  | 固件与轮次 | 观察到的准备态样本 | 准备态区间 ms |
  |---|---:|---|
  | 旧版 baseline-r1 | 6 | 97.180–139.295 |
  | 新版 transition-r1 | 1 | 上界 42.718 |
  | 新版 transition-r2 | 1 | 上界 49.217 |

- 两轮新上界均低于旧下界，支持减少逐拍构图等待；共享 union 仍需先停旧 DMA，
  不能宣称无缝切换。首轮出现一次主站切换 missing，第二轮短窗未记录到 missing；
  两轮后段 accepted 增长且 reject/missing 未再增长。短观察窗口不满足通用长窗口验收：旧版记录原报
  `no complete board record available after failed startup`，新版原报
  `final periodic samples missing`。实际所有板端原件均完整 26 条、missed/reason=0，
  原评估失败保留，未修改门禁让短窗口通过。
- 独立长窗口 `250000 us × 26` 两轮主站 body 为 713.720/698.812 us，外层为
  744.960/728.164 us；两轮均记录到一次主站切换 missing，后段 accepted 增量
  847/842、reject/missing 增量均为零。
  当前较大外层峰值比 057 两轮较大值高 19.436 us，仍远低于早期 903–916 us 增长
  区间，但本切片没有证明运行期收益，也未归因这部分峰值差异。准备态毫秒收益不能
  抵扣完整 phase 微秒预算。两轮 startup barrier 通过，整段 periodic interval 和
  实时门禁仍失败；后段健康不能取消原失败，完整 500 us 继续开放。
- 当前源码真实四板 OTA/P3 已完成；普通短帧 passed/closed_loop/realtime 为 true，
  整体 `strict_gates_passed=false`。TRN-01 SCK 训练失败，TRN-03 无满足 flight re-arm
  budget 的实测 SCK 组合，最佳候选 margin=-1 sample；诊断继续和原始失败保留。
  最终固定矩阵普通恢复通过。基线、P3、两轮短窗、两轮长窗与普通恢复均完成四板
  STOP/config ACK、许可证失效和 SD 逐字节读回。构建、源码、P3 凭证与产物核对见
  `source-checkpoint-r1.json`、各 `*-review.json`、`review-final-r2.json`；代码与文档
  分离提交及封存索引见 `commit-proof.json`、`slice-manifest.json`。
- 下一 gate：按本地接收/转发预备、TDMA PREPARE/READY/NACK、提前约定生效圈与
  排空停止评审生命周期协议。当前两字节 opcode/seq8 没有这些执行语义，固定配额、
  epoch/generation、确认集合、超时/重复/迟到/取消必须先完成 wire 与资源评审；
  同圈与 VDC 合格共同时间下的同时执行分开验收。继续定位切换 missing、完整 WCET、
  SCK 准入与正式 RAM，后续 service blackout/特等逐圈交付/邮箱容量仍未闭合。
  本轮未更改 registry 状态，长期目标保持进行中。

### TDMA-PROGRESS-20260913-057 - 分状态取证与高频路径 SRAM 驻留压回时间增长

- 任务：`TDMA-FLIGHT-002B/002F`。状态：`PARTIAL`。按用户顺序先处理时间增长，
  邮箱随编译节点容量配置排后。HAOFV Core1 唯一 owner、1 kHz 静态表、TDMA 500 us /
  VDC 96 us / SYNC 39.2 us 预算保持；本轮未改 wire、PIO 指令或 DMA/SM 分配。
- 证据根：`out/HardwareAcceptance/20260913/tdma-flight-runtime-growth-fix/`。以下数字
  均为有限采样窗口、当前构建或离线模型快照，非事实源；源代码与原始证据为准。
- 历史归因：051 的主站 body 为 713.480 us，056 为 769.120/754.512 us。前序
  `tdma-flight-peak-growth/analysis-r1.txt` 已把后两者定位在 STOP 前，不能用导出解释
  增长。六个 TDMA TU 的 147 段与 relocation 相同而最终地址改变；本轮未取得 Core1
  专属 cache miss 轨迹，不能把历史差额全部归为缓存。新增 DMA 档案的有效路径每圈
  多 17 个 descriptor、776 B 总读写，这些总线量不能直接换算成 CPU 微秒。
- 计量：`TDMA_SERVICE_TIMING_VERSION` 扩展 owner 双端 state/config generation/
  trial epoch/return sequence 与现有调度器外层区间。原 PEAK 按全部 phase 的 body
  保全，RUN/OTHER 分别按外层峰值保全整条记录。自主类要求两端 state/config/非零
  epoch 一致；不证明区间内硬件状态连续恒定。从站不是自主 origin，RUN=0 不代表
  零耗时。外层包含原 begin/end 探针，不含分类后置发布，不能代替完整 WCET 门禁。
- 同固件档案对照：默认 TRIAL 保持档案；NORECord 只能通过有限诊断许可证在准备
  前冻结，运行图不可变。停止后 OFF 档案必须 UNAVAILABLE。三种随机种子、每模式
  64 圈的实际 builder 总线模型输出逐字节相同，OFF 零档案，非法模式拒绝；默认
  档案的 128 圈 valid/missing/partial/bad-mailbox 覆盖仍通过。首次图对照未重置
  payload RNG 导致输入不同而失败，R2 纠正输入，原失败保留。
- 实施分三步：先将计时 now/record 与 VDC 读钟驻留 SRAM；再驻留固定邮箱 RX
  inspect/unload、发布后的 commit、owner 动态授权复验及上下文读取；最后驻留
  context 累计和 Calibration/RefMem 的两个原子 epoch getter。map seqlock、邮箱
  头/目标/新鲜度检查、发布提交顺序、到期/撤销/代际复验及所有探针保持。A/B 实际
  符号和调用位置见 `residency-review-r1/r2/r3.json`，不是仅凭源码 attribute 判断。
- 固定 `p3-r1/trn03-matrix.json`、板端 `250000 us × 26`，运行期间 SCPI 零查询，
  STOP 后导出。先 ON/OFF/OFF/ON，再对三步优化各采两轮；主站结果如下。body 为
  同条外层峰值的 body，子项是包含式区间，不能叠加为更大的虚构总耗时。

  | 模式 | build | body us | 外层 service us | 后段 accepted/reject/missing |
  |---|---|---:|---:|---|
  | ON R1 | 20260913090342 | 877.848 | 903.248 | 822/0/0 |
  | OFF R1 | 20260913090342 | 855.716 | 877.500 | 820/0/0 |
  | OFF R2 | 20260913090342 | 876.320 | 891.728 | 827/0/0 |
  | ON R2 | 20260913090342 | 891.000 | 915.764 | 818/0/0 |
  | 读钟/计时 SRAM R1 | 20260913092845 | 743.072 | 780.064 | 829/0/0 |
  | 读钟/计时 SRAM R2 | 20260913092845 | 858.016 | 872.296 | 831/0/0 |
  | RX/授权 SRAM R1 | 20260913094619 | 723.928 | 745.856 | 828/0/0 |
  | RX/授权 SRAM R2 | 20260913094619 | 735.312 | 758.492 | 830/0/0 |
  | context/epoch SRAM R1 | 20260913100801 | 697.956 | 720.820 | 832/0/0 |
  | context/epoch SRAM R2 | 20260913100801 | 707.240 | 725.524 | 837/0/0 |

- 当前两轮中较大的 body 峰值相较本轮 ON 基线减少 183.760 us，较大外层峰值减少
  190.240 us；均保留档案。两轮 body 都低于历史 713.480 us 参考值，但 V6 新增探针
  与布局不同，不能把与旧版本的差额当成单变量收益或 WCET 证明。ON/OFF 四轮不足
  以把任意一对差额称为固定档案成本，也未证明档案写入是增长的主要原因。
- 当前外层较高峰的非重叠分解为 RX parse 212.688 us、handoff 其余 20.552 us、
  adapter 其余 289.604 us、owner 其余 115.032 us、body 其余 69.364 us、外层其余
  18.284 us，总计 725.524 us。下一轮降预算仍应沿 adapter/owner 及接收证据发布
  路径定位。当前两轮 NO2/NO3/NO4 的 ALL body 分别为 658.684/657.828/663.856 us
  与 667.104/665.972/636.588 us；独立节点峰值不拼成同一帧。
- 当前 A/B/Boot build `20260913100801`，源码指纹
  `fede03f2120b0d8147c118d9a32679e087cacbb694475887e561fe0c6f6eab1f`，
  1,035 个源码文件。V6 诊断使 link-free 从 3,100 B 到 2,320 B，新增 780 B；三步
  SRAM 驻留利用链接对齐空隙，A/B 余量未再下降。生成 PIO 头与前序逐字节相同。
  这些是链接快照，不代表 formal RAM/运行栈余量验收通过。
- 验证：27 项 timing/admission、63 项 service、22 项 clock/timing、46 项
  engine/授权/资源、26 项 context/epoch 回归，完整 ring-adapter 与 RefMem 表模型
  host 单测及文档检查器测试通过。四个构建均完成真实四板 OTA/P3；第一、三、四版
  QUICK 严格门禁通过。第二版 SCK 覆盖/重装余量失败及新矩阵普通恢复失败保留，
  用既有实测 baseline 矩阵恢复成功。第二版首次 STOP helper 错引旧 build，在发
  STOP 前退出，修正嵌套引用后的恢复 STOP/SD 通过。
- 十轮自主试验的整段 `passed/closed_loop_passed/realtime_gate_passed=false`，
  `diagnostic_passed=true`；切换 missing/DOWN 与 periodic interval 拒绝继续保留。
  后段 accepted/reject/missing 是观测窗口事实，不证明逐圈无损 VDC 交付。当前两轮
  自主、P3 和最终普通恢复均完成四板 STOP/config ACK/许可证失效与 SD 逐字节核对。
- 存储失败保全：第三版 `er-on-r1` 的 NO3 recorder SAVE 为 FAILED/state 9；原始
  RAM 导出结构/CRC/主机 SHA 完整。原文件读回为空，另存恢复也失败；恢复脚本先
  错读 INFO 字段，再因通用匹配器过滤写命令 tuple 超时，按真实事务 offset 有界
  续传后 expected/computed CRC 一致，但 END 仍报存储 error 6。未删除、格式化或
  改动存储实现；不能声称该轮 SD 闭环。原失败、主机原件及继续决定留在证据根，
  后续各轮四板 SD 成功不证明该存储根因已关闭。
- 最终普通恢复使用同一 baseline 矩阵，短帧闭环通过；见 `normal-baseline-r4/`、
  `normal-baseline-r4-stop.json`、`normal-baseline-r4-sd/`。主控复核、代码/文档分离
  提交及封存见 `review-final-r1.json`、`commit-proof.json`、`slice-manifest.json`。
  时间增长已在本轮有限窗口内压回前序水平；完整 500 us、自主切换稳定性、正式
  SRAM、无损逐圈 VDC 消费和 service blackout 仍开放，不提升产品状态或 registry。

### TDMA-PROGRESS-20260913-056 - 既有 DMA 逐圈档案与 STOP 冻结读回

- 任务：`TDMA-FLIGHT-002B`。状态：`PARTIAL`。继续采用已确认的 TDMA 500 us 预算、
  原周期与 HAOFV 静态表。此轮解决有限档案的硬件写入及停止后取证，未完成特等席
  生产态逐圈消费、绝对时间或完整 WCET。
- 证据根：`out/HardwareAcceptance/20260913/tdma-flight-cycle-record/`。以下数字为
  当前源码和本轮硬件快照，非事实源；代码符号与原始产物为准。
- 实现：原 origin loader/executor DMA 通过不可变的八个 writer 入口轮换写入
  `tdma_origin_record_t` 固定池，每槽 48 B，总计 384 B。记录与双 RX bank 独立，
  包括 sequence/identity/local generation、输出与捕获余量、raw RTT、实际返回
  全局 trailer、epoch/format 和首尾 sequence。缺帧、部分返回、坏 mailbox 仍留
  本圈失败事实；transport checked 标志不授予 trailer 完整性、业务 owner 或 VDC
  接受，当前同步 trailer 仍无效。没有新增 DMA/PIO 资源或 Core1 每圈触发。
- 生命周期：完成整个 DMA 树的 STOP 后才发布冻结元数据；停止失败保留 workspace
  责任且不发布。重复 STOP 保留档案，persona 选择和复用前使旧档案失效。新 SCPI
  `READ:CALibration:ORIGin:RECord? <age>` 经 owner facade 提供最新七槽，预留一槽
  排除中途停止的下一次覆盖。复制检查独立 guard、epoch、format、首尾 sequence
  和最大复制期限；没有安装离线原型中的活动态 reader。
- 软件验证：正式 C builder 的实际 AL3 words 在离线总线执行 128 圈，valid/missing/
  partial/bad mailbox 各 32 圈，转发字节与原 transport C oracle 一致；12 项地址/
  容量拒绝，1,016 种 supported mask/local-slot 组合构图通过。六节点占 315 个 run、
  126 个 literal；七/八节点的较大构图容量只做 builder 验证，当前固件仍编译六节点，
  不代表其整机 RAM/部署准入。候选活动态 reader 另有 300 组交错、20 个读取边界与
  229 次覆盖拒绝，仅为离线证据；尾标单检反例保留。
- 相关 Python/C 功能回归与 adapter host 测试通过。首轮三处失败分别为新增 STOP
  元数据未同步测试桩、旧 timing version 常量断言、资源检查器仍扫描旧 start 函数。
  修正后新增负测又发现 TX DREQ 正则可跨越后续语句误认 `true`，收紧参数边界后
  资源检查 30 项通过；其余相关 Python 60 项与 adapter host 回归通过。失败原始
  日志、修正和最终输出均保留。
- A/B/Boot build `20260913074316`，源码指纹
  `982ba20f1c671cd336da20f4f86850a58f718d116fddd61aa296079d0520243f`，
  1,035 个源码文件。实际 ARM workspace 从 7,696 B 到 7,900 B，增长 204 B，消耗
  既有对齐空隙，剩余空隙 292 B；physical owner 从 1,816 B 到 1,832 B。A/B 最终
  链接各增加 16 B，link-free 各为 3,100 B；不能当作 formal RAM/栈余量通过。PIO
  生成头与前序 build 逐字节相同。见 `production-link-r1.json`、`source-checkpoint-r1.json`。
- 当前源码四板 OTA、QUICK P3 的校准和普通 process-image/FIFO 短帧闭环通过，
  `flow_completed=true`、`strict_gates_passed=true` 仅适用于本轮 QUICK 配置；不覆盖
  NO5/DPLL、多源满载、完整 Core1 WCET 或生产态长期稳定性。前序 SCK 失败仍保留。
- 两轮自主试验均按板端 `250000 us × 26` 窗口采样，START 后仅发临时许可证和
  一次 profile RESET，STOP/config ACK/physical STOP 后才查询档案。NO1 的自主
  样本内序号分别增长 3,749、3,750；这些是软件快照的采样事实，不是 wire cadence
  或 Core1 blackout 证明。七条冻结记录分别为 sequence 5,355–5,361、5,372–5,378，
  epoch 为 1、2，完成版本 9,670、9,712。两轮首尾一致、fault=0、capture/output
  remaining=0、transport checked=1、返回同步 trailer=0。重复 STOP 后原档案一致；
  恢复普通 persona 后全部返回 `UNAVAILABLE`。
- 两轮自主整段仍 `passed=false`、`closed_loop_passed=false`，startup 通过后仍有
  切换 missing/DOWN 与 periodic interval 拒绝；本轮没有关闭这些缺口。一次 RESET
  后的累计完整 phase 峰值如下，包含 STOP/idle，不是纯稳态 WCET，也不能与不同
  探针、矩阵、预算表下的旧峰值直接解释为性能增减：

  | 轮次 | NO1 us | NO2 us | NO3 us | NO4 us |
  |---|---:|---:|---:|---:|
  | 自主 R1 | 769.120 | 677.908 | 677.668 | 687.952 |
  | 自主 R2 | 754.512 | 691.788 | 675.012 | 680.992 |
  | 普通恢复 | 996.072 | 654.468 | 627.580 | 657.952 |

- 普通恢复短帧闭环通过。P3、两轮自主、普通恢复的四板 SRAM 记录均在物理 STOP
  后保存 SD，离线解码和逐字节回读一致；最终四板 STOP/config ACK、许可证失效及
  当前矩阵读回通过。主控复核见 `hardware-review-r1.json`；封存与分离提交以
  `review-r1.json`、`slice-manifest.json`、`commit-proof.json` 为准。
- 下一步：沿现有 owner 边界继续减少有界软件准备和提交成本，补充连续边沿与绝对
  VDC 时间映射、生产态有界消费者/覆盖拒绝及切换 missing 根因。固定记录池只保留
  最新有限历史，不能宣布逐圈特等席无损交付、500 us 达标或长期目标完成。

### TDMA-PROGRESS-20260913-055 - 共享边沿计数与 DMA 保全候选离线验证

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002B`。基于 `4cde88d`，证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-edge-counter/`，入口为
  `review-r1.json` 与 `slice-manifest.json`。本项数字均为离线实验快照，非事实源。
  生产源码未改，未执行板卡查询、OTA、ARM 或新的 P3。当前预算仍由已采用的完整
  静态表派生；本轮不改变 500 us 目标，也不声称取得新的 WCET 收益。
- `edge_counter.pio` 经实际 pioasm 汇编后为 10 words，可替代自主 origin PIO1
  的 RTT 与未启用 latch 合计空间，由两个既有 evidence SM 共用；PIO1 总量仍为
  32 words。`edge-model-r1.json` 执行实际重定位指令字，1,460 组波形与 200 组
  双计数器/回绕检查通过，各边沿路径均每五个指令时钟递减一次。在当前时钟下派生
  步长为 20 ns，不能当作含同步器与 epoch 误差的实测精度。启动 LOW 会产生伪首
  记录、短脉冲可漏采、非阻塞 PUSH 会丢数、阻塞 PUSH 会破坏计数映射的反例保留。
- `audit_plan_capacity.c` 编译并执行当前实际 C builder：四/六/八活动槽分别使用
  260/282/304 个 descriptor 和 105/109/113 个 literal；物理预留为 320/128。
  这只是空间核算。源代码确认自主 origin 的旧 RTT 已由 DMA 收取并重装，不能将其
  再次计算为 Core1 下沉收益。现有两份 RX bank observation 也不是逐圈保全队列。
- `handoff-model-r2.json` 使用独立 DREQ 收取、拆分的 FIFO 读取/SRAM 写入与自动
  计数续装模型；80 组各 400 条记录含消费停顿及每组三次自动续装通过，另有 25 组
  回绕扩展和 2,040 组启动时间区间检查。候选寄存器使用 SDK 的 TRIGGER_SELF；
  ENDLESS 不递减计数，不能提供本方案的完成进度。实验双环共 512 B，只保存原始
  计数；示例 DMA9/10 尚未获得静态声明、SDK 独占或资源仲裁准入。
- 负测区分两种丢失：SDK 规定 `FDEBUG.RXSTALL` 记录非阻塞 PUSH 遇满，但不记录
  SRAM 整圈覆盖。循环写指针可回到原位，须结合完成计数检测；计数和指针都回绕的
  无界停顿仍须由读取期限拒绝。FIFO pop 早于 SRAM 完成、复制时写入、STOP 在途写
  退休及旧 epoch reader 均有反例/拒绝检查。缺 RX 后按 FIFO 序号配对会整体错位；
  模型按声明窗口检测缺失与重复，但尚未证明真实 CONTROL sequence/identity 关联。
- 首次 handoff 模型运行因 SDK DREQ 常量采用普通数值定义而非 `_u(...)` 导致
  `KeyError`，保留 `handoff-model-command-r1.log/json`；解析修正后通过，后续补强
  双寄存器同时回绕及时间区间检查。离线模型没有生产 MMIO 顺序、真实 DMA 仲裁或
  总线 WCET 证明，不以 host 通过代替当前源码硬件验收。
- 下一实现边界见 `working-design.txt`：静态准入两路独占证据 DMA，并与旧 RTT
  executor 的 FIFO 读取/暂停/重装一起交接；完整帧同步字段及身份需独立保全，不能
  从已覆盖的 RX 银行补取。VDC/Calibration 负责连续计数到公共时钟的 epoch 与
  不确定度；启动时间区间不能伪装为精确时刻。完成 owner/STOP 树、完整记录池和
  时钟映射后，再执行固件 A/B/Boot、当前源码 P3、板端采集及 STOP 后 SD 核对。
  前序 SCK 覆盖/重装余量、完整 WCET、正式 RAM 与 donor 满载门禁仍开放；本轮
  为离线方案证据切片，`TDMA-FLIGHT-002B` 保持 `IN PROGRESS`，registry 不变。

### TDMA-PROGRESS-20260913-054 - 采用 TDMA 新目标预算与完整静态表迁移

- 状态：`IN PROGRESS` / `PARTIAL`；对应 `TDMA-FLIGHT-002F`。用户确认 TDMA 目标
  改为 500 us，旧 380 us 不再作为长期硬门槛。本条落实静态预算，完整 WCET、
  逐圈特等席与产品准入仍独立验证，不能据预算增加宣布长期目标完成。
- 证据根：`out/HardwareAcceptance/20260913/tdma-flight-budget-500/`。以下数字为
  **2026-09-13 快照，非事实源**；实现事实源为 `config/project_config.h` 中
  `PROJECT_CORE1_PHASE_*`，派生使用 `BOARD_SYS_CLOCK_HZ`。基线为前序已封存的
  `404f3948595ee16ca13f40df60248cb705fa134d`，计划见 `slice-plan.json`。
- 唯一实现修改为静态调度配置：TDMA WCET 从 95000 拍变为 125000 拍，窗口末端
  改为 130000 拍；VDC 让出 18000 拍，SYNC_TRIGGER 让出 12000 拍。相邻执行项起点
  与终点同步平移，没有运行中借用余量；PIO、wire、节点容量、owner 和门禁算法保持。
  当前完整表及生成 SVG 见 `schedule-review-r1.json`、`schedule-500us-r1.svg`。

  | phase | start–end us | WCET us | 保留余量 us |
  |---|---|---:|---:|
  | TDMA | 0–520 | 500 | 20 |
  | VDC | 520–620 | 96 | 4 |
  | DPLL | 620–756 | 136 | 0 |
  | CALIBRATION | 756–816 | 56 | 4 |
  | SYNC_CAPTURE | 816–836 | 16 | 4 |
  | REFMEM | 836–936 | 96 | 4 |
  | MODEL | 936–948 | 12 | 0 |
  | SYNC_TRIGGER | 948–988 | 39.2 | 0.8 |
  | TRIGGER_MEASURE | 988–996 | 8 | 0 |
  | GUARD | 996–1000 | 0 | 4 |

- 静态复核：周期保持 250000 拍 / 1 kHz，每个 phase 原有余量不变，总 WCET 不变，
  guard 不执行负载。三个调整项在各自新 WCET 内通过、超一拍仍拒绝；没有只修改
  报警数字而保留旧窗口。相关 Python 回归 168 项通过，A/B/Boot 和 Flash 门禁通过。
  build `20260913060514`，当前源码指纹
  `922b625fd880891e07d07004c9178ac6cf65b24fbb7745e66e8e689bbb206d17`，
  1033 个源文件，编译容量六节点；A/B 链接余量各 3116 B，PIO 生成头与前序一致。
- 当前源码硬件流程：四板 OTA、复位、P0T、coarse/coded、MARK/residence、DATA
  和短帧完成。`p3-receipt-r1.json` 为 QUICK_DIAGNOSTIC，flow_completed 为 true，
  strict_gates_passed 为 false；SCK 两项原拒绝保留。link0 的八次有效结果全部为
  同一 offset，未达到 candidate_coverage；没有实测行满足 flight re-arm 余量，
  调试选择 `[1,1,0,0]`，最小 follower margin 为 -1 sample。没有手改成未测的安全行，
  也没有重复运行筛选通过结果。`p3-r1/trn01-sck/summary.json`、新矩阵 derivation 和
  diagnostic_failures 保留原判定；本轮不确认 SCK 失败与预算调整的因果。
- 新表实际生效：P3 与 `normal-restore-r1` 的板端冻结记录全部重新解码，逐样本比对
  每个 phase 的 start/end/WCET、时钟与周期，均匹配当前源码。两轮短帧的
  passed/closed_loop_passed/realtime_gate_passed 均为 true，但 diagnostic_continue
  保持启用。每轮先 STOP/物理/config ACK，再 SAVE 和逐字节核对，共八份记录相符；
  最终四板停止、配置应用完成、临时许可证停用。SCPI 只做准备、控制和停止后导出。
- 新预算下的执行证据：恢复记录中 VDC 累计最大 52.452–59.080 us，同步触发
  23.252–24.592 us；观察区间没有这两相位自身 overrun 增量，但有 start-miss/skip，
  未覆盖供出预算相位的最大输入负载。不能以这组低于预算的数字放行满载 WCET。
  同区间四板 TDMA overrun 增量为 1215/359/411/432，完整调度仍失败。
  一次 RESET 后的完整 profile 峰值为 1009.948/639.396/670.028/630.636 us，仍超
  新目标；它包含 STOP/idle，并且是普通 origin 模式，不能与前序自主模式峰值直接
  作优化对比。原始字段、mask、计数与独立复核见 `hardware-review-r1.json`。
- 交付边界：已采用新静态预算并完成当前源码调试硬件闭环；未宣称完整 WCET、
  SCK 严格门禁或产品验收通过。代码、文档分离提交和证据封存见 `review-r1.json`、
  `slice-manifest.json`、`commit-proof.json`，后续性能优化继续按 500 us 目标执行。
- 新目标不降低其余验证要求：VDC 多源与 epoch 转换、同步触发峰值请求/取消/迟到、
  后移 phase 绝对 deadline、完整调度 WCET、正式 RAM、逐圈时间戳与 Core1 blackout
  继续开放。未变更 registry 状态；需要登记状态迁移时仍执行跨域与 C11 审核。

### TDMA-PROGRESS-20260913-053 - 维护命令真实应答与 coarse 配置屏障

- 状态：`IN PROGRESS` / `PARTIAL`；对应 `TDMA-FLIGHT-002F`。本轮修正校准主机
  控制完成判据，不产生 Core1 WCET 优化，不修改固件 C/PIO、生产预算或契约状态。
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-calibration-command-boundary/`；
  以下数字为 **2026-09-13 快照，非事实源**，完整绑定见 `source-checkpoint-r1.json`、
  `hardware-review-r1.json`、`review-r1.json` 和提交后的 `commit-proof.json`。
- 等条件对照纠正：前序独立 QUICK 前置尝试没有显式固定 P3 的 phase 会话环境。
  本轮 `baseline-command-r1` / `corrected-command-r1` 固定相同 QUICK 时序、phase
  生命周期与串口读超时，均软件复位后执行 P0T→coarse；trace 保存响应匹配前原始行，
  不增加运行中状态采样。对照固件为 `20260913034710`，两轮均完成四个 reference。
- 已证明的缺陷：PROBe 开启真实应答 `1,2`、关闭 `0,0` 被公共匹配器丢弃；旧主机
  两阶段共 16 个有效 tuple 被变成 timeout。TOPology、OPMode STAGE/APPLY 另被当成
  普通写命令使用短 ACK 窗口，部分 timeout 被转换为没有自行验证的合成成功。
  `TDMA_CONTROL_RESULT_FIELDS` 统一维护数值结果形状；结构化写命令使用完整 response
  等待，匹配要求启用时无关裸 ACK 不再提前完成，真实 timeout 原样返回。
  修正对照的 16 个 PROBe tuple 全部识别，coarse 保留 16 个 STOP_APPLIED 和
  16 个 ARM_STARTED 记录，requested/applied 配置一致。
- coarse 屏障：STOP 必须等 enabled/adapter 停止与 config ACK，TOPology tuple
  必须匹配完整请求，ARM 后复核实际拓扑与应用代际；迟到读回不能通过 deadline。
  准备动作先留痕，失败也进入全板清理；清理 STOP/PROBe 关闭失败进入失败结论。
  这不是原拓扑错配的完整根因证明，也未修改 P0T/coded/MARK 的准备编排。
- 验证：新用例在旧实现上先出现 10 项预期失败；修正后响应回归、握手回归与相关
  Python 回归通过，完整相关套件 366 项。A/B/Boot 构建与 Flash 门禁通过，build
  `20260913052726`，源码指纹
  `29651877e2589e8731924bac9d05a814eac7821da628003cdc6834af1b166167`，
  1033 个源文件。六节点编译容量保持，PIO 生成头与前序字节一致，A/B 链接余量
  均为 3116 B，新增链接占用为零；不提升正式 RAM 准入。
- 首轮当前源码 P3：四板 OTA、P0T、coarse/coded 通过；MARK 准备中 NO2 在
  STOP/config 59/59 后执行 TOPology 返回 timeout，随后错误队列为
  `-200,"Execution error"`。尚未 MARK ARM/注入，也未生成完整选行或凭证。
  源码 setter 仍有快照、控制锁和 activation 拒绝路径；该证据不支持仅增加等待，
  也不能归结为 STOP 未应用。失败原件保留于 `p3-r1/`。
- 有界恢复：`post-marker-failure-r1.json` 保存身份、当前 build、四板 STOP、物理
  停止、config ACK 和许可证停用；NO2 单独同命令成功，只证明可恢复。随后
  `p3-command-r2` 复用刚 OTA 的同一包，重新复位并实际执行所有四板测量阶段，
  不重放旧结果。`p3-receipt-r2.json` 为 QUICK_DIAGNOSTIC，flow_completed、该配置
  strict_gates_passed 均为 true，diagnostic_failures 为空；当前短帧
  passed/closed_loop_passed/realtime_gate_passed 均为 true。调试继续配置仍保留，
  NO5/DPLL 不在本轮范围；不提升为完整产品验收。
- 恢复采集顺序失败：`normal-restore-r1` 在旧记录仍 FROZEN 时请求 RECord ARM，
  记录器按现有生命周期拒绝，未 START。raw 仍为上一 epoch 的完整冻结记录；
  `diagnostics_tdma_record_arm()` 只接受 IDLE/SAVED/FAILED，根因是本轮辅助流程
  漏做 SAVE。先重新核对 STOP/物理/config，再将 P3 原记录 SAVE 并逐字节核对；
  `normal-restore-r2` 以 SAVED 前置条件重做普通模式板端采集。记录及停止后的 SD
  读回分别见 `p3-r2-sd/`、`normal-restore-r2/`、`normal-restore-r2-sd/`；最终状态
  见 `final-state-r1.json`。首轮恢复失败没有被覆盖。
- 下一步仍按 PIO 容量实况推进：follower TX 可评估首边沿自动保全；自主 origin
  要先压缩、复用或重排程序。时间戳收割/重装、固定 LOAD/UNLOAD 与 DMA 计划复用
  由 TDMA owner 静态授权，保持 sequence/epoch、唯一 FIFO 所有权和既定资源分区。
  500 us 是预算候选，完整 WCET、逐圈特等席、Core1 blackout 和多板长稳继续开放。

### TDMA-PROGRESS-20260913-052 - PIO 状态机下沉核算与校准前置失败留证

- 状态：`IN PROGRESS` / `PARTIAL`；完成资源与候选审计、有限校准对照和四板恢复，
  没有生产实现变更、新 WCET 收益或严格产品验收结论。`TDMA-FLIGHT-002` 继续 active。
- 证据根：`out/HardwareAcceptance/20260913/tdma-flight-calibration-arm-generation/`。
  当前硬件沿用前序构建与源码指纹；本条数字均为 **2026-09-13 快照，非事实源**，
  绑定信息见 `pio-offload-assessment-r1.json`、`review-r1.json` 和 `commit-proof.json`。
- PIO/SM 核算：读取实际 persona loader、board/resource 契约、PIO 源文件及前序构建
  生成头，逐程序核对指令数。每组 PIO 的四个 SM 共享指令空间；flight owner 声明
  整组 SM，空闲执行槽不代表可由其他 owner 借用。占用快照如下：

  | persona | TX PIO 指令字 | RX PIO 指令字 | 说明 |
  |---|---|---|---|
  | 普通 origin | 25/32 | 12/32 | 不能把该余量沿用到自主 persona |
  | 自主 process origin | 32/32 | 32/32 | RX helper 已承担 DMA 比较；TX latch 槽位/程序保留但未武装 |
  | process follower | 14/32 | 32/32 | TX 剩余 18 字仍须验证 GPIO、DMA、FIFO、生命周期与准入 |

- 已有下沉：follower DATA SM 已完成延迟转发、白名单 overlay 和 RX 卸载，DMA
  descriptor 负责续转；自主 origin 的 header/CRC、旧值与本地 shadow 选择、RTT
  收割/重装也已有硬件执行图。不能把已有能力再次算作本次新增节省。PIO 无通用
  SRAM 读写，全局 DMA sniffer 是独占资源；继续保持 PIO0/DMA7 的既定分区。
- 后续候选：优先比较连续首边沿 latch 与已有 executor 收割/重装方案，绑定
  node/sequence/epoch 并计算最短圈周期下的保留容量；再复用固定装卸与不可变
  DMA 准备结果。增加采样 SM 必须保持唯一业务 FIFO 消费者，缺边沿、FIFO 满、
  计数回绕、STOP 与 Core1 缺席均需独立证明。普通 `push noblock` 不保证特等席
  每圈保全；相对 RTT 不是绝对 VDC epoch。RX map/health/发布后 commit 仍需 owner
  语义，不能把整个 RX_PARSE 搬入 PIO。前序距 500 us 的缺口仍为约 157–213 us，
  本条不承诺新增 SM 足以消除它；生产门限未修改。
- 校准调查：`baseline-command-r1/r2` 保留 coarse 原判断与逐命令 raw trace，四个
  reference 均通过，停止后配置代际相等。第二轮包含 reset/P0T，但使用 FULL
  时序，不能称为原 QUICK 失败的等条件复现。源码追溯表明 RefMem 聚合状态最终
  也读取 TDMA owner，尚无证据支持“独立 RefMem 缓存”根因。
- 实际 QUICK 前置流程见 `baseline-prepare-command-r2`：NO3 OPMODE APPLY 超时、
  active level 仍为零，在 pair ARM 前失败，原日志保留；未继续盲目重试 coarse。
  FULL 中部分 TOPology ACK 已超过 QUICK action timeout，这是时序线索，仍需
  ACK/配置应用完成交错证据，不能据此确定原拓扑错配根因或只扩大全局 timeout。
- 恢复与原始复核：首次停止检查收到两板许可证 `UNAVAILABLE`，脚本解析失败留存；
  同次 runtime/physical 已停止且配置代际相等，缺样没有解释为许可证失效。
  随后使用前序当前源码矩阵恢复普通模式；`normal-restore-r1` 短帧闭环通过，采样由
  板端有限记录完成，RUN 期间 SCPI 只发控制命令。`final-state-r1.json` 复核四板
  物理/运行态 STOP、config ACK、矩阵和许可证停用，随后 SD SAVE/读回四份记录
  逐字节一致。恢复成功不关闭 QUICK 失败、完整 WCET、RAM 与逐圈特等证据缺口。
- 验证：源/生成头容量一致性、文档完整门禁及主控 raw 记录解码复核。
  现有状态机资源检查器报告 overlay TX DREQ/FIFO 两项失败，raw 输出保留；源码
  中二者已由 `tdma_pio_spi_phys_overlay_binding()` 生成并传给 plan，检查器仍只
  扫描 start 函数。本轮不修改检查器或把源码追溯改写为自动门禁通过。
  证据摘要与单独文档提交见 `review-r1.json`、`slice-manifest.json`。
  本轮不重放或重签前序 P3，也不以普通恢复代替新实现的 P3 验收。

### TDMA-PROGRESS-20260913-051 - Core1 RX 接收提交分项计时与严格失败保留

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`，关联 `TDMA-FLIGHT-002B`。
  以下数字为实验快照，非事实源。证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-rx-accept-timing/`，基线提交
  `b2e64131dbb9dc747cf1d2a65adf357526b89021`。代码/文档分离提交及证据 SHA 见
  `commit-proof.json`、`slice-manifest.json`，不 push。
- 实现：`TDMA_SERVICE_TIMING_VERSION` 扩展为版本 `5`，在 RX_PARSE 内新增 inspect、
  health、evidence、FIFO publish、commit、complete 子区间。剥离新增 `14` 行计时
  语句后，接收处理源文件与基线逐字一致；保留成功发布后的新鲜度提交、原拒绝条件、
  owner 与生命周期。主机保留旧版本解码，前序四板实际 profile 原始响应仍解码为
  相同记录；板端二进制采样格式保持。见 `build-review-r1.json`。
- 软件与资源：计时测试 `19 passed`、相关回归 `190 passed`、文档测试 `18 passed`；
  A/B/Boot 和 Flash link gate 通过。计时工作记录增长 `48 B`，最近/峰值快照增长
  `96 B`，总新增 BSS `144 B`；A/B 链接余量均由 `3260 B` 降为 `3116 B`，BSS 前
  对齐空隙仍为 `1752 B`。新增探针的时间与内存成本不从门禁中扣除。
- build `20260913034710`；源码指纹
  `36cc2be4b8fcad38ea1a9f858ca723aaf8189bd06c91c3baaaaf3a13a6ab3240`、文件数 `1032`。
  编译容量保持六节点，PIO 生成字节与固定 wire 保持；未借用新 PIO/DMA 资源。
- 首轮真实 P3 `run` 完成四板 OTA、校准流程和普通短帧。短帧 startup/soak 通过，但
  粗 CLK 校准读回拓扑不一致，SCK 校准和重装余量行选择也失败。第二轮使用同一包及
  OTA 证据执行真实 `resume`，重新校准；SCK/矩阵和普通短帧通过，粗校准仍读到
  node count `2` 的运行状态，与该步请求的四节点拓扑不一致。两份凭证均保留
  `strict_gates_passed=false`，不进行相同条件的盲目第三次复测。
- 当前 staged 凭证选择第二轮 `QUICK_DIAGNOSTIC`，只证明同源码诊断流程完成，不能
  以 receipt `passed` 或提交门禁接受替代严格验收。粗校准 helper 在 STOP 后仅留
  固定 gap，再写 topology/ARM；该步没有使用已有停止等待函数，启动读回也不绑定
  本次配置应用代际。这是待验证的时序线索，尚未证明为根因；下一步先保留该控制
  流逐条应答并核对 STOP、拓扑应用和 ARM。见 `coarse-state-review-r1.json`。
- 首次 SD 保存 helper 被停止预检查拦截：P3 的 `left_running=true`，helper 读到
  runtime 仍启用，未发送 SAVE。原失败和空导出文件保留；随后显式 STOP 并确认
  physical/runtime、配置应用及许可证状态，再在新目录保存与逐字节读回成功。
  第二轮也先 STOP 再 SAVE；这是调用顺序遗漏，不是已发生的 SD 写入故障。
- 自主取证沿用板端 `250000 us × 26` 窗口，START 后仅发临时许可证和一次 profile
  RESET；STOP/撤销后统一导出，采集窗口内零 SCPI 数据查询。原启动门禁通过，自主
  总评仍失败：保留普通 persona/TX 计数谓词、切换 missing，以及 NO1/NO4 的 DOWN/
  恢复事件。后段本地窗口未见 missing/reject 增长，但从站观察丢弃仍增长；不能把
  这些周期性状态采样当作每圈时间证据。原记录和分析见 `hardware-review-r1.json`。
- 同次完整累计峰值及子项如下，单位为 `us`，均是实验快照。每板各有一个峰值 phase，
  包含 RESET 后至读取之间的 STOP/idle；不是纯稳态 WCET，也不是跨板同一圈。

  | 节点 | 完整峰值 | RX_PARSE | inspect | health | evidence | FIFO publish | commit | complete |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|
  | NO1 | 713.480 | 216.388 | 89.420 | 20.816 | 21.372 | 15.168 | 26.696 | 5.108 |
  | NO2 | 676.168 | 268.688 | 76.128 | 35.904 | 26.676 | 18.892 | 57.092 | 15.600 |
  | NO3 | 660.992 | 293.704 | 66.164 | 35.280 | 31.532 | 34.424 | 58.756 | 12.508 |
  | NO4 | 656.528 | 293.004 | 75.868 | 49.952 | 48.984 | 29.908 | 31.500 | 8.928 |

- 上表各新子项在相应完整峰值中调用一次，父子区间不得相加。inspect 与 commit
  都涉及 map/固定头遍历，是后续准备结果复用的优先项；总区间也包含计数与 owner
  操作，不能把整个区间视为可消除的重复复制。health 和 FIFO 发布的 payload 复制
  仍存在，但实测不能支持把它们当作 RX_PARSE 的全部成本。本切片只新增归因能力，
  不是执行时间优化；完整 `380 us` 门禁继续拒绝。
- 对上述完整峰值按调用层级作互不重叠的总账复核，见同目录
  `timing-analysis-r1.json`、`timing-analysis-r1.txt`。达到现门禁仍需压缩约
  `42%–47%`；即使假设 RX_PARSE 全部消失，NO1/NO2 仍约 `497/407 us`。这是预算
  算术，不是收益预测。NO1 独立 origin 路径使 overlay 项为零，adapter 仍有约
  `268 us` 未细分，包含发车准备及共同分支，不能全归给 TX。后续还须压缩 owner
  外围并检查 Core0 积压；不可通过提前 freshness、少处理帧或丢弃逐圈时间戳达标。
- 用户追加评估 `500 us` 候选：当前 TDMA 窗口仅 `400 us`，后续即为 VDC；各 phase
  WCET 合计 `959.2 us`，单改 TDMA 后为 `1079.2 us`，超出当前周期。保持现有相位
  余量和尾部 guard 需真实释放并重分配 `120 us`，不能借用相邻窗口。当前峰值即使
  对照候选仍需减少约 `157–213 us`。同目录分析保留从代码生成的全表、候选校验拒绝
  和两档预算算术；候选可作后续优化里程碑，正式调整还需全负载及跨域复评。
- 用户进一步要求增加预算后，优先形成保持当前周期的 `500 us` 完整静态候选：VDC
  窗口由 `172 us` 改为 `100 us`，同步触发由 `88 us` 改为 `40 us`，分别供出
  `72/48 us`；WCET 同步调整并保持原各相位余量与尾部 guard。候选通过现有全表
  静态检查，生产配置未改。留存四轮调度记录中 VDC 累计最大为 `66.004 us`、同步
  触发为 `24.788 us`，可支持优先验证此方向；样本包含启动/STOP、skip 与隔离，
  不构成全负载 WCET。DPLL/Sync Capture 已有累计超限继续保留，不能挤压它们。
  完整候选、原始 JSON 落点和负载覆盖条件见 `budget-assessment-r2.json/txt`；首轮
  本地分析脚本名称错误失败保留，修正后重算。当前不再以坚守旧门限为唯一目标，
  下一预算 gate 是供出相位的全负载复评、绝对 deadline 与跨域审查。
- 使用第二轮当前矩阵恢复普通模式，原 startup/soak 通过。两轮 P3、自主取证和恢复
  共 `16` 个有效板端文件，重新解码身份、CRC、全部有效字段与零漏采，并逐字节核对
  SD；停止前被拒绝的保存尝试另行保留。最终四板 STOP、配置应用、相位和许可证
  inactive 通过，heap min 均为 `20344 B`，正式 RAM 仍失败。见 `final-state-r1.json`、
  `final-status.json`、`hardware-review-r1.json`。
- 文档、源码指纹、真实 P3 与失败保留的主控复核见 `review-r1.json`。下一 gate 是
  校准状态代际拒绝的闭合，以及有界 RX inspect/commit 准备结果复用；不得提前提交
  新鲜度或绕过 map/epoch。硬件 blackout、同圈更新、逐圈特等席、完整波形与长稳
  继续开放；registry/C11 不变，长期目标保持 `PARTIAL/active`。

### TDMA-PROGRESS-20260913-050 - 自主交接板端取证与接收提交成本归因

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002B`，关联 `TDMA-FLIGHT-002F`。
  以下数字为实验快照，非事实源。证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-autonomous-board-evidence/`，基线提交为
  `095fea8499040babd50829953a0901d81130dda3`。本轮仅新增诊断证据与文档，没有生产
  源码、固件或记录格式修改；提交与原始证据 SHA 见 `commit-proof.json`、
  `slice-manifest.json`，不 push。
- 四板沿用 build `20260913025147`、源码指纹
  `af9cab8c8b4ac3f368d61f2162c55d26e8140200f04c96c9b908267ed5616216`、`1032` 个文件。
  `source-checkpoint-r1.json` 绑定前序严格 P3 凭证和新测矩阵行；本轮未重编译或 OTA。
  既有 P3 仅证明同源码普通短帧基线，不能提升为本轮自主模式已验收。
- 取证：先 ARM 板端记录器与 START，再按固定延迟发一次 Calibration 临时授权和
  一次 profile RESET；运行窗口内没有 SCPI 数据查询。STOP、撤销后导出冻结记录与
  累计峰值，owner STOP/config ACK 后才 SAVE/读回 SD。首次每板 `500000 us × 20`
  样本，第二次改为原 `250000 us × 26`，启动期限 `2 s` 与三次连续稳定要求未放宽。
  两轮均保留完整文件和命令；有限窗口记录不等于持续 RUN 写 SD。
- 首轮启动失败：第三个稳定样本完成时已超过原期限。保留 `first-probe-review.json`
  的早期解释，并在 `autonomous-review-r1.json` 更正：`periodic_interval_gate_failed`
  汇总区间错误，不能归因于采样周期本身。第二轮启动通过，整体原评估仍失败；NO1
  还触发普通 persona/TX 计数谓词，四板各增长一次 receive missing，NO4 在本板约
  `2.25 s` 的样本中 DOWN、下一样本恢复。模式谓词不适配与真实切换失败分别保留。
- 自主证据：首轮 NO1 在本地 `2.50–9.50 s` 样本持续 persona `16`、FSM `5`；第二轮
  为 `2.25–6.25 s`。样本间接收序号约每秒增长 `1000`，NO1 接收接受数约每秒 `484`。
  该比率是软件快照证据，不是物理线速、每圈波形或同圈多节点更新证明；各板触发
  时刻独立，不能直接按相同 slot 对齐。本轮 Core1 service 始终启用，没有 blackout。
- 第二轮各板本地 `4.5 s` 之后的约 `1.75 s` 描述性区间均 UP/DOWN 运行，missing、
  reject、transport bad 与 bitmap incomplete 没有增长；原失败不因此撤销。
  NO2/NO3/NO4 的 RX observation drop 分别增长 `684/679/626`，RX ring overrun
  增长 `333/336/434`，接收接受数约每秒 `390/391/357`；FIFO publish/mirror drop
  为零也不能覆盖上游观察丢弃。逐圈时间戳须独立保全，普通可丢弃镜像不能承担特等席。
- 第二轮同次完整累计峰值：NO1/NO2/NO3/NO4 为
  `705.280/658.376/653.224/650.224 us`，对应 RX_PARSE 为
  `137.372/255.180/276.784/248.620 us`。这些峰值含 RESET 后的 STOP/idle；嵌套
  子项不能相加，也不能与板端不同纪元时钟的采样时刻直接匹配。完整 `380 us` 门禁
  继续拒绝，未据此放宽预算。
- 源码复核：RX_PARSE 包围 prepared 帧的 Core1 接收提交，decode 与 origin mailbox
  CRC/诊断已由 Core0 准备。`tdma_flight_engine_fast_mailbox_header()` 不计算 CRC；
  unload 和 FIFO 发布成功后的 commit 会各读取 map/固定头。仅确认存在重复遍历，
  尚未测得它在总时间中的占比，不能宣称删除该遍历即可达标。后续先隔离健康判定、
  时间关联、FIFO 发布与 owner 提交成本，保持不可变输入、generation/epoch 与提交
  顺序；证据和下一门禁见 `source-review-r1.json`。
- 使用当前矩阵恢复普通模式，原启动/短帧评估通过；三轮合计 `12` 个板端文件重新
  解码身份、CRC、终止状态、有效字段和零漏采，SD 字节均一致。最终四板物理/runtime
  STOP、配置应用、相位与许可证 inactive 确认通过，heap min 均为 `20344 B`，正式
  RAM 仍失败。见 `autonomous-review-r1.json`、`final-state-r1.json`、`final-status.json`。
- 验证与封存见 `review-r1.json`、`commit-proof.json`。文档门禁独立执行，生产源码
  指纹再次复核；本轮是诊断取证切片，不是新的固件验收凭证。下一步保留切换失效
  定位，推进有界 Core1 RX 提交，并独立闭合硬件 blackout、同圈更新、特等席逐圈
  保全、完整波形及长稳。registry/C11 状态不变，长期目标保持 `PARTIAL/active`。

### TDMA-PROGRESS-20260913-049 - RX 捕获控制 SRAM 放置与保留失败的板端对照

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-rx-capture-residency/`，基线为
  `1aabad0bea6ea0c88833ddb8cfe0b44099e3bcf3`。代码/文档分离提交，提交与原始证据 SHA
  见 `commit-proof.json`、`slice-manifest.json`，不 push。
- 实现：仅为 `tdma_pio_spi_phys_capture_words_async()` 增加 SRAM section 和 noinline。
  函数体逐字保持，Core1 的完成区间、几何、复制、覆盖/epoch、私有头检查和失败处理
  顺序不变；Core0 继续只提供不可变发现提示。未修改 owner、生命周期、PIO/DMA、固定
  wire、节点容量或预算。不能把该控制函数驻留当作所有被调函数都已驻留。
- 软件与资源：相关测试 `223 passed`、文档测试 `18 passed`，A/B/Boot 与 Flash link
  gate 通过。A/B 捕获控制函数均位于 `0x20008970`，各占 `936 B`，新增七个 SRAM 调用
  跳板另占 `56 B`；`.data` 总增量 `992 B`。BSS 前对齐空隙从 `2744 B` 降到 `1752 B`，
  BSS 和链接末端保持，link free 仍为 `3260 B`。见 `build-review-r1.json` 与
  `resident-calls-review-r1.json`；该空隙不能当作后续通用 RAM 配额。
- build `20260913025147`；源码指纹
  `af9cab8c8b4ac3f368d61f2162c55d26e8140200f04c96c9b908267ed5616216`，文件数 `1032`。
  首轮真实 `run` 完成四板 OTA，但 `strict_gates_passed=false`：SCK 校准及重放行准入
  失败，所选行 follower 最小余量为负，随后短帧启动超时，NO1 坏帧/拒绝持续增长。
  原矩阵、诊断、板端记录与 SD 文件保留；不能用流程退出成功或 receipt 的 `passed`
  字段替代严格门禁。见 `p3-receipt-r1.json` 和 `hardware-review-r1.json`。
- 同一固件使用前序已验矩阵完成诊断对照后，第二轮 `resume` 复用已验真的 OTA 包，
  重新执行真实四板校准和短帧，并非离线 replay。`p3-receipt-r2.json` 的
  `strict_gates_passed=true`，无诊断失败；该 QUICK_DIAGNOSTIC 凭证绑定当前源码。
  第二轮在固定矩阵对照之后运行，已应用新测矩阵并确认实际相位和 STOP，复用该闭环
  作为矩阵恢复证据，没有再执行一轮相同采集。首次失败的根因与校准长稳仍待定位。
- 采样仍由板端时钟驱动，每板 `250000 us` 周期、`14` 个样本及独立基线。性能对照
  在 START 后只发一次 profile RESET 控制，STOP 后导出冻结记录和累计峰值；标准 P3
  在板端窗口结束/冻结后导出。所有 SD SAVE/读回都在 owner STOP 与配置确认后执行。
  累计峰值含 RESET 后的 STOP/idle，不能提升为纯稳态 WCET；嵌套子项保持同一完整
  phase，不拼接独立最大值。
- 同一前序矩阵的完整对照见 `profile-comparison-r1.json`。NO2/NO3/NO4 完整峰值从
  `713.560/707.576/689.448 us` 降为 `635.340/618.296/631.744 us`；同峰值内 RX capture
  从 `265.364/259.208/239.940 us` 降为 `201.748/209.768/219.840 us`。
  NO1 普通 origin 完整峰值从 `1131.052 us` 变为 `981.976 us`，对应 capture 为
  `43.208→24.832 us`；不能将整个峰值变化都归因为捕获代码放置。各运行的峰值对应
  不同 phase，局部耗时差也不是所有调度拍的稳定余量。
- 另保留首轮基线峰值导出缺口：NO3 的 `PROFILE:PEAK?` 返回 `UNAVAILABLE`，原 helper
  保留 null/不匹配标记但没有让短帧采集失败。该轮板端记录与 SD 字节有效，未用于
  四板峰值比较；第二轮完整基线使用新的 helper，STOP 后最多八次读取，逐次保留响应，
  仍不可用或 reset generation 不匹配则拒绝。没有增加采集窗口内的串口数据查询。
- 两轮基线、两轮 P3 和固定矩阵候选对照共 `20` 个板端文件，身份、CRC、`14/14`、
  零漏采/溢出和 SD 逐字节核对均通过；其中首轮 P3 的通信失败事实独立保留。最终四板
  STOP、配置应用、实际相位和许可证 inactive 已确认，heap min 均为 `20344 B`，正式
  RAM 仍未通过。见 `hardware-review-r1.json`、`final-state-r2.json`、`final-status.json`。
- 结论：保留有界捕获控制驻留的局部收益，完整 `380 us` 门禁继续拒绝。本切片未新增
  自主 blackout 或完整原始波形，仍须推进交接/latch、owner/adapter 和普通 origin
  发车开销，随后独立闭合同圈 owner/CRC 更新、特等席逐圈保全、故障恢复与长稳。
  registry/C11 状态保持，长期目标仍为 `PARTIAL/active`。

### TDMA-PROGRESS-20260913-048 - RX 复制 SRAM 放置与板端峰值对照

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-rx-copy-residency/`，基线为
  `ca55dd9b2585e070f294e7f0611224d09176d04c`。代码/文档分离提交，提交及原始证据 SHA
  见 `commit-proof.json` 和 `slice-manifest.json`，不 push。
- 实现：仅将 `tdma_pio_spi_phys_rx_ring_copy()` 叶函数放到 SRAM 并禁止内联。函数体
  逐字保持，归一化、volatile DMA 读取、已完成区间、复制后覆盖/epoch 复验和同次取帧
  内的 latch 绑定均沿原路径；未改 Core1 owner、生命周期、PIO/DMA、固定 wire、节点
  容量或 phase 预算。A/B 实际指令均在 SRAM，包含 RBIT 且没有外部调用。
- 资源与软件：相关测试 `223 passed`、文档测试 `18 passed`，A/B/Boot 和 Flash link
  gate 通过。叶函数各占 `216 B` SRAM，`.data` 增长由 BSS 前对齐空隙吸收，该空隙从
  `2960 B` 降到 `2744 B`；BSS、链接末端及 `3260 B` link free 保持。不能把对齐空隙
  当作任意后续改动的免费 RAM。见 `build-review-r1.json` 和 `ram-padding-review-r1.json`。
- build `20260913022156`；源码指纹
  `2792c55257e742bb7193dd21af5cb08475e8f11ff7292fdd5b2e6cb7c4d30cea`，文件数 `1032`。
  当前源码真实 `run` 完成四板 OTA、校准及短帧，`p3-receipt-r1.json` 的
  `strict_gates_passed=true`，无诊断失败；本凭证仍为 QUICK_DIAGNOSTIC 范围。
- 采集：使用现有板端记录器，每板 `250000 us` 周期、`14` 个样本和独立基线。运行
  窗口内 SCPI 只发一次 profile RESET 控制，不查询实时数据；STOP 后统一导出 RAM、
  读取板内累计峰值并保存/读回 SD。累计峰值含 RESET 后至读取之间的 STOP 和 idle
  service，不能称为纯稳态 WCET；保留峰值的完整同次 phase，各嵌套子项不相加。
- 同一前序矩阵、相同板端采集条件的结果见 `profile-comparison-r1.json`。NO2/NO3/NO4
  同峰值内复制耗时分别由 `127.756/159.100/150.888 us` 降为
  `17.176/20.948/22.976 us`；完整峰值分别由 `722.452/750.820/744.968 us` 降为
  `706.140/688.964/686.100 us`。对应 capture 仍为 `246.556/239.048/249.636 us`，
  RX handoff 为 `327.308/325.112/318.716 us`。NO1 普通 origin 完整峰值为
  `1058.524 us`，该峰值未执行复制，不能把它的变化归因为此叶函数。各记录属于不同
  phase/运行窗口，不能把局部差值直接作为全表稳定余量或 XIP 争用的完整因果证明。
- 失败保留：首次基线采集和二进制导出成功，但后处理调用遗漏 node index；第二次
  ARM 因原冻结事务未保存而被拒绝。原 helper、命令、错误及文件全部保留，先将首次
  冻结记录保存到 SD 后才完成第三次基线。没有清空缓冲、复位或改状态以伪造成功。
- 基线、候选对照和新测矩阵恢复的短帧门禁均通过。包括首次恢复出来的基线记录在内，
  共 `20` 个板端文件逐一通过 CRC、身份、漏槽和 SD 逐字节核对，每个文件均为 `14/14`、
  零漏采/溢出。新测矩阵恢复后最终四板 STOP、配置序号、实际相位和许可证 inactive
  均核验；heap min 均为 `20344 B`，正式 RAM 门禁仍未闭合。见 `hardware-review-r1.json`。
- 结论：保留复制热路径的局部收益，完整 `380 us` 预算仍未通过。下一步继续处理
  live capture 剩余固定开销、交接/latch 与 owner/adapter 工作；普通 origin 的发车
  耗时仍须经自主硬件路径和 blackout 独立闭合。持续 SD、逐圈时间戳、同圈交换、
  故障恢复、完整波形与正式资源验收继续开放，不改变 registry/C11 状态。

### TDMA-PROGRESS-20260913-047 - 板端自主记录、冻结导出与停机 SD 验证

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-board-recording/`，基线为
  `81183d93c310651c32206456341a979acf9ae1d6`。提交与原始证据 SHA 见 `commit-proof.json`
  和 `slice-manifest.json`；代码/文档分离提交，不 push。
- 实现：`diagnostics_tdma_record` 由既有 Core0 Storage task 独占推进。SCPI 提交 ARM/
  START/CANCEL/SAVE 意图；采集期不能读取变化中的缓冲。ARM 保留基线，START 绑定本板
  请求时刻，板端按固定目标槽采集既有 owner 快照。完整保留 runtime/process/FIFO/
  physical/CRC 健康项及所有调度 phase，包括 GUARD；用变更位图压缩，未复用 SCPI parser
  做采样。目标/开始/完成时间、序号、有效位、漏采和终止原因均进入文件，不循环补样。
- 所有权与存储：复用现有 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES` 写事务，新增旧值向量
  和少量状态。FROZEN 后可统一导出 RAM，SAVE 在 STOP 和配置序号确认后才交给 StorageAO。
  本地证据封口仍通过正常 commit 的独立长度/CRC 检查；普通上传不获得放宽。Core1 phase、
  PIO/DMA、wire 和六节点容量保持。此实现是有限窗口实验，不是持续 SD 记录或逐圈采样。
- 软件与构建：相关回归 `286 passed`、文档测试 `18 passed`；测试执行生产记录器、真实
  portable OTA CRC 和实际 Storage commit，覆盖正常采集、延迟漏槽、容量耗尽、取消、
  保存重试、身份/CRC/截断拒绝、原健康条件、迟到样本及中途掉线后恢复。首轮构建由 phase
  数量断言发现遗漏 GUARD，修正后 A/B/Boot 和 Flash link gate 通过。另保留两条旧串口
  主流程断言失败及其向板端行为测试迁移的证据。
- build `20260913013149`；当前源码指纹
  `f25fabe20f0f5e4a6d1ca77a54b4dc4082dee13ef144405652bb5bcc37279db4`，文件数 `1032`。
  A/B 静态 RAM 各增加 `1572 B`，link free 为 `3260 B`；既有存储缓冲仍为 `16384 B`。
  实际反汇编局部帧 `record_sample=3160 B`、`app_record_snapshot=2656 B`，不能当作完整
  调用链栈界。初次审计漏识别 `subw`，原报告保留，修正见 `build-review-r2.json`。
- 第一轮真实 `run` 完成构建、四板 OTA 和 P3，但 `strict_gates_passed=false`：SCK 校准
  与 TRN-03 重放行准入先失败，随后 NO1 接收拒绝/坏帧增长，启动与 soak 均未通过。
  四板记录仍各完成 `14/14`、零漏采，原文件在 STOP 后落 SD 并逐字节读回。失败数据
  见 `p3-r1/tdma-process-image/` 与 `sd-recordings-r1.json`，不能归因为记录器或被重测覆盖。
- 同一源码/固件复用已验真的 OTA 包，第二轮 `resume` 重新进行真实四板校准与闭环，
  不是离线 replay。`p3-receipt-r2.json` 的 QUICK_DIAGNOSTIC 严格门禁通过；启动在本板
  请求时刻后的最坏 `0.759863 s` 达到所需连续稳定间隔，原 `2 s` 门限保持。初始两次
  不健康观测和 pipeline fill 拒绝仍在。soak 接收好帧增量依次 `304/304/303/304`，坏帧
  增量均为零；这不是长稳误码率证明。
- 第二轮四板各保留 `14` 个周期样本及独立基线，采集周期 `250000 us`，零漏采/溢出，
  文件大小依次 `7804/7472/7524/7560 B`。记录快照读取区间的中位数约 `3.438–4.221 ms`，
  包含 Core0 抢占与 getter 等待，不是 Core1 action 时间；不能直接提升到逐圈采样。
  TDMA phase 累计最大值仍约 `1251–1495 us`，包括启动历史，未据此宣布稳态 WCET 通过。
- 第二轮 STOP 后 SD 保存控制事务约 `0.156 s`，四个文件与原冻结 RAM 逐字节相同，见
  `sd-recordings-r2.json`。未使用 Flash，未验证 RUN 持续写盘。最终四板 STOP、配置应用
  确认、当前相位与临时许可证 inactive 均通过；Storage 栈余量至少 `5288 B`，SCPI 为
  `2408 B`，heap min 均 `20344 B`，仍低于正式 RAM 门禁。
- 结论：有限板端记录与 SD 后处理子项通过，长期目标仍为 `PARTIAL/active`。后续定位
  首轮 SCK 重放不稳定与 SD RUN 等待，收敛高密度记录成本和原 owner/runtime RX handoff。
  完整 `380 us`、blackout、同圈交换、逐圈时间戳、故障恢复和当前完整波形继续独立验收。

### TDMA-PROGRESS-20260913-046 - Core0 有界输出、原启动门禁闭合与板端记录转向

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-serial-observation/`，基线为
  `8cb7ac95ef5306c4cc77d3968c7b0a9cdb6103d5`。提交号与原始证据 SHA 见 `commit-proof.json`
  和 `slice-manifest.json`；代码/文档分离提交，不 push。
- 归因：完整查询中的串口读取与字节到达记录见 `serial-arrival-r1.json`、
  `serial-arrival-analysis-r1.json`。已排队字节的读取中位成本约 `13–21 us`，较长响应
  到达跨度约 `0.321–0.461 s`；主要等待不是主机处理积压字节。实际 parser 将数字和
  分隔符分开写入，SDK 默认驱动每次输出执行 USB service/flush。此证据不定位具体板端
  task/IRQ 的全部耗时，也不构成 silicon event time 或 Core1 WCET 证明。
- 实现：仅 Core0 默认 stdio 路径使用 `SCPI_PORT_STDIO_BATCH_BYTES` 栈缓冲合并小片段，
  借用现有 context 槽，函数返回前排出尾部并恢复指针。用满、显式 flush、路由切换及
  错误日志前排出待发字节；capture/custom 语义保持。没有新增静态缓冲或 Core1 action，
  不改 TDMA 生命周期、PIO/DMA、wire、owner、节点容量或预算。
- 软件与构建：相关测试 `264 passed`、文档测试 `18 passed`。真实生产 C 和真实 parser
  验证分片/重复输入、长二进制响应、复位前显式 flush、捕获截断/canary、custom 路由、
  错误退出和 context 恢复。前三次测试 harness 的定义提取、MinGW visibility 及 query
  换行预期修正均保留；未据测试修改 parser 语义。A/B/Boot 与 Flash link gate 通过，
  build `20260913005845`；源码指纹
  `a3e9e6fff7842cd1b4f3679eec7cdfc8c518c721c44dcfbc061ff2ca4d0ace8a`，文件数 `1026`。
  六节点、生成 PIO 保持，A/B link free 均 `4832 B`。实际 service 局部栈帧由 `48 B`
  增至 `136 B`、writer 由 `24 B` 增至 `48 B`；局部帧不等于完整调用链栈上界。
- 原观察门禁闭合：当前源码 P3 QUICK_DIAGNOSTIC 流程的 `strict_gates_passed=true`，
  MARK 与全部轮换 origin、SCK、DATA 测量通过。原 `2 s` 启动条件下，初始样本仍记录
  incomplete/rejected 增长，后两个健康样本在 `0.969/1.437 s` 完成，原门禁通过。
  独立完整五查询对照中，前后各 `32` 组 RUN 单板采样中位数 `1.208 → 0.358 s`，
  约下降 `70.4%`；最大值 `1.712 → 0.442 s`，第三个稳定样本由超时 `6.672 s` 提前至
  `2.156 s`，原 `6 s` 截止通过。实际应用相位相同，校准 identity、链路实测及计数
  仍变化；有限对照不保证固定收益比例。见 `query-comparison-r1.json`。
- 通过范围与存储失败分开：独立窗口 `1.938 s` 内四板 good 增长
  `473/475/484/475`，bad 增长为零。波形运行短帧门禁也通过，但 SD RUN 保存仍超时，
  `diagnostic_passed=false`；其 `passed=true` 不得提升为波形通过。STOP 后只读取回
  原四份文件；NO2 当前完整帧 transport 有效，其余三板仍无完整帧。SCK 中位门禁通过，
  不证明最坏抖动或失败首圈。最终四板停止、配置应用、相位与许可证读回通过；SCPI 栈
  最低余量 `2612/2612/2408/2612 B`，任务水位门禁通过；最低堆均 `20352 B`，正式 RAM
  仍失败。证据见 `hardware-review-r1.json`。
- 用户进一步明确 SCPI 只触发流程，不做实时采样；上述五查询对照在该要求前完成，
  后续不再扩展该方案。下一切片改为板端时钟驱动的固定容量记录，先进入 RAM，再由
  Core0 Storage owner 保存到 SD，结束后统一导出；先定位现有 RUN 保存等待，保留
  原健康/时限/覆盖证据。整体仍为 PARTIAL，完整 TDMA WCET、正式 RAM、service blackout、
  同圈交换与特等席逐圈保全继续开放；门限与节点容量后续项顺序保持。

### TDMA-PROGRESS-20260913-045 - 启动观测成本归因与默认 SCPI raw 分片输出

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-startup-observation/`，基线为
  `8a5efe7f8fdc916b48b17b6a65ce1e78d8013f10`。代码/文档分离提交，提交号、门禁及原始
  文件 SHA 见 `commit-proof.json` 与 `slice-manifest.json`；不 push。
- 归因：未修改的 `sample_node` 完整执行 runtime/process/FIFO/physical/CRC 五查询。
  基线 STOP 采样约 `0.162–0.211 s`，RUN 采样显著变慢；逐事务开始/完成时间、响应字节
  和完整快照见 `stopped-query-cost-r1.json`、`running-query-cost-r1.json`。真实 A/B
  反汇编及所用 SDK 路径确认默认 `scpi_port_write` 每字节调用 raw stdio、进入 USB
  驱动；不据此断言所有运行期延迟或初始参与位图拒绝的根因已经闭合。
- 实现：默认 writer 按已有 parser fragment 调用同 raw flags 的 `stdio_put_string`，
  按其整型长度接口有界分段；保持原字节、CR/LF、不附加换行。capture/custom stream
  的路由优先级、截断和返回语义不变；无新增静态缓冲，不改 Core1 路径或运行状态边界。
  `implementation-plan.json` 保留 stdout 串行化持有时间与既有传输返回语义的限制。
- 软件与构建：相关回归 `256 passed`、文档回归 `18 passed`；真实 C writer 测试覆盖
  含 NUL/高位字节的分片、空写、截断/canary、capture 优先级及 custom 部分返回。
  A/B/Boot 与 Flash link gate 通过，build `20260913002647`，源码指纹
  `8e4e9e9577a75f454982aeaad5f6e98406fc38483cbb8e767fde9d4a3c2bab00`，文件数 `1026`。
  六节点、固定 wire 和生成 TDMA PIO 保持；A/B link free 均为 `4832 B`，无静态 RAM
  增长。源码、产物与驱动调用检查见 `source-checkpoint-r1.json`、`build-review-r1.json`。
- 硬件与观察对照：当前源码 P3 QUICK_DIAGNOSTIC 流程完成，MARK 准备与四个 origin 的
  residence、SCK、DATA 测量通过；严格通过仍为 false。相同五查询、原 `6 s` 截止和
  三个连续稳定样本条件下，前后各 `32` 组 RUN 单板采样的中位数从 `1.592 s` 降到
  `1.208 s`，约下降 `24.1%`，最大值从 `2.047 s` 降到 `1.712 s`。运行计数及响应长度
  会变化，故该有限比较不等于固定比例或 WCET 保证；见 `query-comparison-r1.json`。
- 原严格失败完整保留：基线第三个健康样本在 `7.485 s` 完成；当前在 `6.672 s` 完成，
  仍超过截止。P3 首样本在 `1.703 s` 完成并观察到初始 incomplete/rejected 增长，
  下一健康样本在 `3.250 s` 完成，超过其 `2 s` 截止。波形运行第三个健康样本也在
  `6.844 s` 才完成。各后续有限 soak 窗口通过；独立对照当前窗口实际 `6.250 s`，
  四板 good 增长 `1506/1507/1504/1505`、bad 增长均为零。不得把后续稳定覆盖启动失败。
- 波形与资源：RUN 中 SD 保存再次超时；STOP 后按原 job/path/generation/epoch 只读
  取回四份文件，见 `waveform-recovery-r1.json`。四板 SCK 中位频率均为 `10 MHz`，
  中位占空比门禁通过；NO2 找到完整长度候选但 transport 为 `BAD_MAGIC`，其余三板未
  找到完整帧。这不证明全板有效帧、最坏周期抖动或失败首圈；原位流和 SVG 保留。
  初次证据汇总因错误假定无效 transport 仍有 sequence 字段而失败，改为保留完整结果
  后复核通过；未修改采集数据。最终四板 STOP/config/phase/license 读回通过，最低堆
  余量均为 `20352 B`，低于正式 `24576 B` 门禁；任务栈水位通过。
- 结论与下一步：当前切片为 PARTIAL。先继续区分主机事务、板端输出及运行负载成本，
  定位 SD 保存与捕获完整性缺口；完整 owner/RX handoff 拆分仍须保持不可变输入与保留
  期限。`380 us`、完整 WCET、正式 RAM、service blackout、同圈交换及特等席逐圈保全
  继续独立验收；本次不调整预算、registry/C11 或节点容量后续项顺序。

### TDMA-PROGRESS-20260913-044 - MARK 准备结果、异步停止确认与注入前身份复核

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-marker-preparation/`，
  基线为 `15a641fe71dd2fa04a2b1ddef672e2fd52f3a42f`。代码/文档分离提交，最终提交号及
  原始证据清单见该目录 `commit-proof.json` 与 `slice-manifest.json`；不 push。
- 原始缺陷与边界：`baseline-reproduction-r1.log` 复现共享串口匹配器丢弃固件拓扑
  成功结果 `4,2,0`，随后 helper 把超时合成为未经执行的“读回已验证”。原 MARK 准备只
  查板号，未等待异步 STOP/config acknowledgement。operating profile 的成功元组也
  存在同类丢弃。此次仅改主机工具和测试，不改固件生命周期、PIO、wire、owner 或预算。
- 实现：共享匹配器保留拓扑与 profile 结果元组，相关命令超时保持原值。MARK 准备记录
  命令前错误队列、结果和命令后错误，等待 runtime/adapter 停止及 requested/applied
  generation 相等后才继续配置；板号仅用于物理映射。所有板的准备结果均保留，失败
  阻止 ARM；prepared record 的训练 identity 或共同 CRC 不一致时阻止注入并执行训练
  STOP。runtime CSV 字段定义移到共享 parser，字段顺序不变。
- 修正过程也保留为证据：首次 Python 收集失败来自循环导入，移出共享字段后修复。
  首次 P3 的 MARK 行已通过，但新增检查误将轮换 MARK origin 与固定 TDMA reference
  比较，拒绝后三个 residence 试验，继而导致新矩阵及旧凭证 fallback 不可用。对照
  `calibration_manager_request_marker_training` 的 `.reference_node = origin_node` 修正
  映射并补四种轮换测试；第一次失败、STOP/许可证读回及两份源码 checkpoint 均保留。
- 软件与构建：最终相关 Python 回归 `252 passed`，文档回归 `18 passed`；完整命令见
  `related-tests-r5.json`、`docs-tests-r1.json`。A/B/Boot 构建和 Flash link gate 通过，
  build `20260912235402`；最终源码指纹
  `ddefd7f992a4677dcc6be0d765040c96f15a965ead08d6652a32e059a5872f3a`，文件数 `1025`。
  工具映射修正不改变固件产物；最终 P3 再次执行 build。六节点、生成 PIO 与前序一致，
  A/B link free 均为 `4832 B`、无静态 RAM 增长；见 `source-checkpoint-r2.json`。
- 四板硬件：`p3-command-r2.json` 完成当前源码 QUICK_DIAGNOSTIC 流程，严格通过仍为
  false。MARK 行及四个 origin 的 residence、SCK、DATA 测量通过。拓扑命令分别返回
  `4,0,0 / 4,1,0 / 4,2,0 / 4,3,0`，错误查询清零，停止/应用确认完整；prepared topology
  generation 与 topology/profile/schedule CRC 全板一致。当前复核见
  `marker-review-r2.json`、`hardware-review-r1.json`；不能据此反推旧 NO3 究竟在哪个
  owner 发生拒绝或部分发布，固件多 owner 拓扑事务风险仍未修复。
- 严格启动与有限收发窗口分开：P3 首次采样在 `1.781 s` 完成，四板初始参与位图与接收
  拒绝计数增长；第二次健康采样在 `3.656 s` 完成，已超过原 `2 s` 截止。波形运行的
  `6 s` 截止内取得两个健康间隔，第三个健康采样在 `7.609 s` 完成，仍不得通过。
  单次整组主机查询约 `1.7–2 s`，属于验收观察成本，不是 Core1 的微秒执行时间，
  不能用于放宽 `380 us`。P3 和波形运行的后续 soak 分别为 `7.110 / 7.688 s`，四板
  good 分别为 `1771/1775/1776/1778` 和 `1875/1867/1869/1869`，transport bad 均无增长，
  已检查的物理故障计数在这些窗口内无增长；上述窗口不能覆盖启动或停止边界。
- 波形：运行中 SD FILE_WRITE 再次超时，原错误保留。STOP 后以有界只读方式取回原
  四份文件，绑定当前 build、generation `1789258030`、epoch `1789258152`，未重采。
  `waveform-recovery-r1.json` 及其 `analysis/*.svg` 显示 SCK 中位频率 `10 MHz`，
  duty 为 `52/48/48/48%`，当前频率/duty gate 通过；仅 NO2 找到完整有效 RX transport
  帧（sequence `3934`、flags `5`）。这不证明其余板完整帧、最坏周期抖动或失败首圈；
  采集发生于后续 RUN，startup timeout 和 SD timeout 均未提升为通过。
- 最终状态：`final-state-r1.json` 核验四板 runtime/engine/physical 停止、配置代次一致、
  当前测量 phase 读回一致，临时许可证全部 inactive。任务最小余量 `556 B`，heap 最低
  空闲均为 `20352 B`，低于正式 gate；完整 RAM/WCET 未接受。文档门禁、正常 pre-commit
  与 staged-source gate 记录在本目录；本切片 PARTIAL，长期目标 active。
- 下一 gate：分别定位首圈 bootstrap 参与证据、主机观察成本与拓扑多 owner 的部分修改；
  保留原健康项、截止语义和失败帧证据。上述边界闭合后再继续 RX handoff、时间戳快速
  通道与完整 WCET 收敛，不用有限无坏帧窗口替代严格启动或逐圈时间戳保全。

### TDMA-PROGRESS-20260913-043 - 启停异步准入与后台队列退休

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-lifecycle-admission/`。基线
  `5829116036b3442a9e539fe4e18a9c00f1e0fa0d`，上一目标轮分类为 progress。本轮
  不登记新冻结契约、不改变 registry/C11 状态；其他设备的单板改动保持独立。
- STOP 先经 Core0 有界管理保护关闭准入并发布禁用请求，不获取队列锁；Core0 后台
  仅在同一 requested/applied generation 已确认且物理 adapter 停止后取消队列与
  recovery 存储。ARM 只发布一次配置，物理应用确认后才开放准入，移除 resume 锁忙
  导致的 disable 回滚。管理保护冲突仍可在发布前拒绝，不能宣称所有管理拒绝已消除。
- 所有权复核补齐“已选中但尚未执行”的交错：Core1 先有界完成已有通用传输，再通过
  `tdma_ring_runtime_service_with_stop_gate()` 确认 STOP；运行中等待业务窗口仍服务
  常驻环路。foundation profile 替换先完成停止/退休，配置 scheduler 时不短暂开门。
  大块池清理留在 Core0。此保证针对生产绑定的 traffic scheduler；永久 PENDING 的
  adapter 会阻止停止确认，不能强清存储。现有全部跨核写者及 HAOFV-879 未在本轮闭合。
- 真实 C 的冻结时钟、锁忙、拷贝中断、物理 STOP 失败、PENDING 完成与 generation
  回绕共 10 项通过，包含在相关 Python 65 项中；另有 traffic scheduler、service
  scheduler、ring runtime、RefMem 四组 host runner 通过。旧同步测试的首轮失败
  原件保留，后续按真实 Core1 应用与 Core0 退休边界推进，缺失 adapter 必须保持
  准入关闭。文档检查器回归 18 项通过。
- Release A/B/Boot 为 `20260912231352`，源码指纹
  `96a0b36045b0f919c94e7ced3dc97eaee241bfe0a533ef12e424d4fdc04df12d`，源码文件 1025。
  六节点、生成 PIO 与前版相同；A/B 各增加链接 RAM 16 B，link free 各为 4832 B。
  指纹、产物、PIO 字节对照与 map 见 `source-checkpoint-r1.json`，正式 RAM 仍未通过。
- 当前源码四板 OTA/P3 流程完成，凭证 `passed=true`、`strict_gates_passed=false`。
  粗 CLK、coded marker、Latency Cal 通过；MARK 偏移行仍因 `topology_crc32`、
  `schedule_crc32` 不一致失败：NO3 的 topology generation/CRC、schedule CRC 与
  其他三板不同，四板 marker capture 均完成且 reject reason 为零。原始身份值见
  `hardware-review-r1.json` 的 mark_trials；不得将其简化为线路相关失败已消除。
- 同配置 24 轮用时 27.031 s：96 次 ARM 全部接受且读到对应物理应用确认；104 次
  STOP 全部应答 OK、紧随错误查询为零、物理停止与配置确认收敛。前版同范围有一次
  STOP 应答失败；有限试验支持当前准入修正，但不证明所有任务抢占组合均可靠。
  三次完整配置另有 12 次 ARM 全接受、20 次 STOP 全应答成功，用时 142.485 s。
- P3 和三次完整配置均保留严格启动超时，后续有限 soak 各通过。P3 窗口 7.078 s，
  good 为 1747/1745/1749/1745；三次窗口为 7.219/7.344/7.843 s，good 分别为
  1812/1820/1818/1816、1771/1772/1765/1763、1951/1947/1948/1952；这些窗口内传输
  CRC 与检查的物理故障计数无增长。启动首样本仍有接收拒绝/bitmap 不完整增长；
  data-run-00 后续在截止前仅取得两个稳定样本，未满足该次要求的三个。不改超时
  或样本门限来覆盖失败，窗口通过不等于严格闭环通过。
- 部分停止边界 NO1 的 TX timeout、origin DATA timeout/recovery 从零读到
  30（P3 后首次 STOP）、26（data-run-00）、53（data-run-02）；data-run-01 无增长。
  各运行重新初始化计数，不能跨段累加。并行主机 STOP 不证明物理同时停止，这些
  前后读数不能单独确定事件时点或因果。原始命令、前后状态与计数全部保留。
- `waveform-r1` 再次严格启动超时；运行期间 SD SAVE job 超时，NO1 仍为 RUNNING。
  停止后一次有界只读恢复取回四板原任务文件，generation/epoch/build 全绑定，见
  `waveform-recovery-r1.json` 及其原始 JSON/SVG。原超时不改为通过。四板 SCK
  中位频率均 10 MHz、占空比 44/52/48/52%，现有频率/占空比门禁通过；只有 NO2
  有完整 RX transport 帧且 CRC 有效，其余三板未找到完整帧。捕获属于后续运行
  窗口，不是启动拒收当帧；不能证明四板整帧链路、最坏周期抖动或逐圈时间戳保全。
- 最终四板均 STOP，engine/physical/requested 关闭、config requested/applied
  一致、当前测量相位核验通过、许可证 inactive。任务水位通过现有检查：最小
  free 556 B，SCPI 各 2700 B，RefMem 4952/4976/4976/4952 B；heap 最低仍为
  20352 B，heap/正式 RAM 门禁仍 FAIL。水位仅覆盖本轮负载。
- 代码与文档分离提交，门禁和封存见 `review-r1.json`、`commit-proof.json`。
  本切片 PARTIAL，长期目标 active：继续闭合启动位图、身份发布与停止/SD 边界，
  当前 380 µs、blackout、同圈交换及特等时间戳逐圈保全门禁保持；节点容量后续项
  仍后置，不用本轮应答改善替代完整列车调度验收。

### TDMA-PROGRESS-20260913-042 - Core0 任务浮点上下文保护

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-fpu-context/`。基线提交
  `76bc4f13b12de35b3519452b8698a02cda6f2666`，上一目标轮完成提交和原始证据纠错，
  分类为 progress。本轮不登记新冻结契约、不改变 registry/C11 状态。
- 追查非法 default map 时，发现 `FreeRTOSConfig.h` 禁用 FPU 上下文保存，但真实
  RP2350 softfp 镜像已使用高位浮点寄存器，包含 Core0 的 Calibration/RefMem 路径；
  旧 A/B 的 `PendSV_Handler` 均未保存/恢复 `s16-s31`。这构成独立任务隔离缺口，
  不能根据该发现直接把历史 ARM result 5 或 result 8 归因于浮点寄存器损坏。
- 启用 `configENABLE_FPU`，使用既有 FreeRTOS port 的条件保存/恢复及 FPU 设置。
  本轮不改变 TDMA 生命周期、owner、wire、PIO 或 gate；Core1 仍按原 schedule 运行。
  新软件 FP 保存区每个使用扩展异常帧的任务占 64 B，来自既有 task stack，不能用
  静态 RAM 无增长掩盖任务栈额外用量，硬件自动异常帧也需在实板水位中核算。
- `test_rp2350_fpu_context.py` 交叉编译真实 non-MPU PendSV 函数，检查生成指令的
  条件 FP 保存、基本寄存器及 DCP 区布局；模型在两个浮点任务交替及中间插入普通
  任务时验证寄存器/PSP/EXC_RETURN/PSPLIM/DCP 恢复。关闭保存的对照重现高位 FP
  内容被后续任务覆盖。独立 3 项及相关集成 129 项通过；模型不模拟硅上异常进入、
  lazy stacking 或切换时延，也不能替代物理抢占验收。
- Release A/B/Boot 为 `20260912223717`，源码指纹
  `670154f8c8a51227714a5f92f8fef0ef954deb3bf1a3577fe09e427280469b41`，源码文件 1025。
  实链 A/B 上下文均通过相同模型，旧 A/B 保留为失败对照；完整指令与哈希见
  `source-checkpoint-r1.json`。六节点、生成 PIO 相同；静态链接 RAM 无增长，A/B
  link free 仍各 4848 B，正式 RAM 验收仍开放。

- 当前源码四板 OTA/P3 的诊断流程完成，凭证 `passed=true`、
  `strict_gates_passed=false`。coarse CLK 中 NO4 ARM result 8；coded marker 的
  参考 NO4 完成超时，原始 capture/DMA 计数为零；严格启动屏障超时均保留。P3 后续
  有限 soak 为 passed，四板 good 增量 1894/1890/1891/1891、传输坏帧零；该窗口
  不覆盖启动和 STOP。当前实测 TRN03 矩阵 `passed=true`，不代表整个 P3 严格通过。
- 相同有界 ARM/STOP 对照：24 轮的 96 次 ARM 全接受；104 次 STOP 中 103 次应答
  成功，NO1 stop-04 timeout，紧随命令读取 `-200,"Execution error"`。用时 30.062 s，
  未 START DATA。前版同范围为 93 次 ARM 接受、两次 STOP 错误；该有限差异不能
  建立浮点修正与 map 拒绝的因果，也不证明管理面锁冲突已消除。
- 三次完整配置尝试保留全部结果：data-run-01 在 NO4 ARM result 8 拒绝，无运行
  窗口，不能计作 soak 成功；data-run-00/02 均严格启动超时，后续有限 soak 各跨
  7.828/7.891 s，四板 good 增量分别为 1877/1877/1881/1880、1934/1934/1930/1932，
  传输坏帧和已检查物理故障在这些窗口内无增长。三次严格标志均 false，诊断标志
  true；外层收集成功只代表证据齐全。此集合用时 125.016 s，未复位或发证。
- STOP 边界仍有故障读数：P3 运行后首次 STOP，NO1 TX timeout、origin DATA
  timeout/recovery 从零读到 15；之后 ARM/STOP 集合重新初始化了物理计数。两次
  完整数据试验 STOP 前上述计数为零，STOP 后分别读到 1 和 7，最终为 7。最终
  四板观察副本丢失各读到 1。保留 `hardware-review-r1.json` 中各阶段原始前后值；
  不跨重初始化直接累加，也不把状态发布时点等同于实际故障时点。
- `baseline-status.json` 与 `final-status.json` 按 UID/build 读取 RTOS 原件。新板
  任务水位均通过现有门限：最小 free 仍为 556 B，SCPI 各为 3064 B，RefMem 为
  4952/4960/4952/4952 B；部分任务水位下降，符合额外上下文需要但不是 WCET 证明。
  四板 heap 最低余量仍为 20352 B，低于现有检查器要求，heap/正式 RAM 门禁仍 FAIL。
  水位只覆盖本轮负载，不能证明最坏异常嵌套或所有任务抢占组合。
- 最终四板 ARM/engine/运行请求已停、config requested/applied 一致，当前实测
  phase 核验完成；四板许可证读回 inactive。本切片状态 PARTIAL，长期目标 active。
  任务上下文配置缺口已修正，ARM/STOP 部分接受、停机超时、完整 phase/WCET、
  blackout、同圈交换和特等时间戳逐圈保全继续开放；下一步回到管理面握手修复。

### TDMA-PROGRESS-20260913-041 - ARM 拒绝归因与管理面部分接受复现

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-arm-rejection/`。基线提交
  `1c2f238791beac1ac19aa9084bbf99919b655768`，上一目标轮有提交和新证据，分类为 progress。
  本轮不登记新冻结契约，不改变 registry/C11 状态。
- 旧 ARM map 拒绝合并了 `tdma_service_configure_flight_map()` 的快照/运行态拒绝及
  engine 的校验/写者冲突/活跃拒绝。新增 service/engine `configure_*_checked` 路径
  返回实际拒绝点；原 bool API 作为等价包装保留。RefMem ARM 对快照不可用和 runtime
  活跃复用已有结果，对 map 写者冲突和 engine 活跃分别报告新诊断枚举；现有
  `SYSTem:TDMA:RING:ARM:STATus?` 可读取。成功路径、原准入顺序、map/generation
  写入和拒绝计数不变；没有新增轮询、自动重试或静态对象。
- 真实 service/engine 测试覆盖两个快照 writer、请求仍启用、adapter 未停、map guard
  忙、engine 活跃、CRC 不符、成功和旧 bool API。拒绝不替换 map 或推进 generation；
  真实 ARM 前端在受控依赖下覆盖所有 map 结果及早期/后续准入，不越过拒绝继续 ARM。
  service 测试 7 项、前端测试 1 项、集成 121 项（含 service）、物理 DMA/RX 32 项及
  真实 adapter C 回归通过。engine 旧配置函数与新受检函数除返回值外逐字等价，其他
  engine 算法未改，见 `source-checkpoint-r1.json`。
- `stop-reproduction-r1.json` 用真实 service/runtime/scheduler 复现另一条独立路径：
  traffic scheduler writer/reader 暂持锁时，STOP 已发布 `ring.enabled=0` 和新 config
  generation，随后 suspend 抢锁失败，命令返回 false，admission 仍开。释放锁后再次
  STOP 成功。该 host 证据证明部分接受缺口存在；旧实板 STOP timeout 未读取紧随其后
  的错误队列，尚不能直接归于该分支，也不能把停止读回当成原应答成功。
- `arm-reproduction-r1.json` 在真实 service/runtime/scheduler 中复现合法配置先发布、
  scheduler resume 抢锁失败再 STOP 回滚：config generation 推进两次、最终禁用，
  config reject 计数未增；释放锁后同一配置可接受。这是 runtime-config 拒绝的一条
  已证实源码路径，尚未证明实板拒绝瞬间持锁者；本轮只增加诊断，没有修复握手。
- Release A/B/Boot 为 `20260912220459`，源码指纹
  `dde8127cc0d01a145fe5a7b3128915a8cc71a878069397e50ffd7da741ff98e9`，源码文件 1024。
  六节点和生成 PIO 相同，无新增静态 RAM，A/B link free 各 4848 B，正式 RAM 仍 FAIL。
- 当前源码四板 OTA/P3 诊断流程完成，凭证 `passed=true`、
  `strict_gates_passed=false`。coarse CLK 的 NO2 与 process-image 的 NO4 均 ARM
  result 8；coded marker 的参考 NO2 被 Manchester/marker flags 拒绝，参考 NO4
  试验中 NO1 ARM result 5。后者在诊断拆分后仍是非法 map，不能解释为 map 锁忙。
  process-image 未交付运行环路，P3 本身没有 soak 通过证据。当前 TRN03 实测矩阵
  `p3-r1/trn03-matrix.json` 的 `passed=true`，不能据此覆盖整套 P3 的严格失败。
- 原始 `lifecycle-r1.json` 及逐命令文件记录 24 轮、96 次 ARM：93 次接受，NO2
  arm-00 拒绝 result 5，NO4 arm-03 与 NO3 arm-13 拒绝 result 8；NO2 stop-07
  与 NO4 stop-18 应答 timeout。上述五次均紧随命令读到 `-200,"Execution error"`。
  STOP 后状态收敛不等于该命令应答成功。最初摘要漏记拒绝，主控逐文件审查已纠正，
  原失败审计 `hardware-review-command-r1` 保留。该集合仅 ARM/STOP，不 START DATA，
  总用时 38.906 s，无复位、发证或配置修改；不能称为无拒绝或负载下长稳。
- 三次重应用当前实测配置并 START 的对照，ARM 均为 1、应答 OK、即时错误队列零。
  三次严格启动屏障均超时：首个样本四板接收拒绝/bitmap incomplete 增长，随后只有
  两个健康样本在截止前完成；后两次第三个健康样本完成时已超时。保留原 deadline，
  没有延长超时或修改通过条件。后续有限 soak 窗口分别跨 8.141/8.141/7.641 s，
  各节点 good 增量依次为 1992/1992/1991/1995、1967/1964/1959/1960、
  1822/1829/1831/1818；传输坏帧及已检查物理故障在这些窗口内无增长。
  `data-run-00..02/summary.json` 均 `diagnostic_passed=true`、严格标志 false；
  这些窗口不包含完整启动/停机边界，也不是自主 blackout 或特等席逐圈保全证据。
- `hardware-review-r1.json` 保留跨启动/STOP 的物理读回：NO1 在三次 STOP 前读到
  TX timeout、origin DATA timeout/recovery 均零，STOP 后分别为 5/10/15；最终仍为
  15，四板观察副本丢失各读到 1。运行窗口的零增长不能抹去这些停机边界读数，计数
  发布时点与实际故障时点还需关联。最终四板应答 OK、runtime/engine/physical 已停，
  requested/applied config 一致，当前实测 phase 已核对。读回许可证 inactive；本轮未
  发证。相关原始命令、状态、CRC 诊断及哈希集中于上述 review。
- 本切片状态 PARTIAL，长期目标 active；维持完整 phase 门限，不声称 WCET 改善。
  下一步先闭合管理面准入关闭、配置接受/回滚、Core1 完成和后台退休的握手，并追查
  非法 default map 的来源及停机超时；禁止无界等待或将大块取消清理移入 Core1。
  当前完整预算、正式 RAM、严格启动、同圈交换和特等时间戳保全仍开放。

### TDMA-PROGRESS-20260913-040 - 锁存与线路时长等价算术

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-latch-arithmetic/`。基线提交
  `13a91180d434b076a0db92c7bd9c56bb43ba9911`，registry/C11 状态不变。
- 前版自主从站 RX handoff 为 430.880/483.924/475.548 µs，已超过完整门限。
  实链显示 RX/TX latch 重装与 RTT 分辨率都调用 64 位软件除法。本轮引入
  `tdma_pio_spi_phys_latch_resolution_ns()`，利用完整输入范围的分子上界证明，
  保持相同舍入并改为硬件整数除法。物理后端三个函数只替换分辨率表达式，时钟
  读取、FIFO 清理、restart/pull/mov/jump、epoch 记录、enable 与拒绝顺序均保持。
- `tdma_pio_spi_phys_wire_time_ns()` 在 bits 不超过 `UINT32_MAX` 且位周期为整数
  时使用等价乘法，该分支也证明原式的分子不会溢出。其他输入继续原有加法、乘法和
  向上舍入除法，包括大诊断尺寸的既有无符号 wrap 行为；baud 为零的回退保持。
  完整整数证明、源码差异等价核验和 A/B 汇编路径见 `arithmetic-review-r1.json`。
- 算术/锁存模型 11 项、相关集成 120 项、物理 DMA/RX 32 项以及真实 adapter C
  回归通过。主机用独立整数 oracle 检查商/半整数边界、全范围随机频率、整数/非整数
  位周期与 size_t/uint64 边界；同一真实线路时长函数体分别按本机 ABI 和显式
  RP2350 尺寸算术运行。真实重装/RTT 函数在受控 PIO/clock 总线上验证拒绝、操作
  顺序、epoch 与最新区间选择。host 模型不能替代硅上 WCET 或帧/边沿因果证据。
- Release A/B/Boot 为 `20260912212759`，源码指纹
  `5f65ac70caf763b8ee47c14b61b727616299d8b13264801f9e314ae002bd31cd`，源码文件 1023。
  A/B 对应三个物理路径已无软件长除法、均有硬件 UDIV；线路时长包含硬件快速分支和
  原通用回退。六节点与生成 PIO 相同，无新增静态 RAM，A/B link free 各 4848 B，
  正式 RAM 仍 FAIL。见 `source-checkpoint-r1.json`、`source-review-r1.json`。

- 当前源码四板 OTA/P3 的诊断流程完成，凭证 `passed=true`、
  `strict_gates_passed=false`；SCK repeat 未接受、没有满足 re-arm 预算的实测 SCK 行、
  严格启动屏障超时均保留。独立严格矩阵生成也明确拒绝，见 `matrix-command-r1`。
  当前测量只能派生 `matrix-diagnostic-r1.json`，其 `passed=false`，不能称为已接受校准。
- P3 快照至停止读回间，NO1 TX timeout、origin DATA timeout/recovery 各从零增至 6，
  last_error 为 `TX_BUSY`；其余节点已检查物理故障无增长。STOP 应答成功，单次有界
  复位成功；计数不能定位事件发生于运行还是停止，也不能归因于本次修改。
  `lifecycle-audit-r1.json` 保留原始边界，不以后续干净窗口消除这些失败。
- 首次计时编排在任何硬件操作前，被诊断矩阵的 SCK 严格加载检查拒绝，原件见
  `autonomous-command-r1`。第二次编排使用显式诊断恢复路径；旧脚本与失败日志保留，
  固件和产品校准检查未改。恢复仅允许本轮测得的非通过矩阵作有界诊断，不提供准入。

- 同条件对照继续使用前版 RX14 旧诊断行、TX 相位、250 MHz 和负载 mask，逐样本
  复核配置。普通首末跨度 25.110 s（7 样本），自主 86.751 s（19 样本）；transport
  good 增量为 6091/6066/6073/6108 和 41264/34425/33216/30327。这两个窗口的
  transport bad 与已检查物理故障增量为零，不能外推到 P3/停止/恢复或逐圈无损。
- 普通 latch count 增量为 6119/12128/12136/12262，自主为 0/68772/66342/60576，
  miss 无增长、读回分辨率均为 8 ns。自主 origin 的此计数未增长，不能据此宣称其
  特等席路径被实际覆盖；从站计数增长也不证明逐圈帧/边沿因果配对。
  三从站普通 overlay prepare 增量 6051/6052/6120，selected 增量 6052/6051/6121；
  自主分别为 24271/24828/25647 和 24271/24827/25646，证明后台计划仍在持续选择，
  不证明 SENT、wire completion 或同圈多 owner 交换。
- 下表为自主窗口完整 profile 峰值及**同条峰值记录**的 RX latch 子项，单位 µs。
  三从站该子项下降，但 NO1/NO3 的完整峰值上升；有限不同窗口仍含分支、XIP/IRQ
  干扰，不能把局部差异全部归于算术指令，也不能宣称整段 WCET 得到一致改善。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版 RX latch | 本版 RX latch |
|---|---:|---:|---:|---:|
| NO1 | 864.488 | 904.776 | 0（无调用） | 0（无调用） |
| NO2 | 839.368 | 817.756 | 62.292 | 54.308 |
| NO3 | 871.176 | 910.324 | 51.680 | 48.968 |
| NO4 | 872.328 | 857.520 | 65.172 | 50.840 |

- 普通完整峰值为 1261.520/866.964/869.128/858.056 µs；自主完整 SCHEDULE 超限
  增量为 50251/58011/58629/56076。自主从站 RX handoff 为 424.336/455.224/
  493.616 µs，单项仍超过完整门限。380 µs 门禁继续 FAIL；不能把约 240 µs 的物理
  飞行全额扣出异步 CPU 预算，也不能靠小幅放宽门限覆盖当前缺口。profile 不含
  recorder 发布，SCHEDULE 独立复核；嵌套阶段不相加，稀疏 last 不充当子项 WCET。
- 普通 RX ring overrun/observation drop 无增长；自主 NO2/NO3/NO4 分别增加
  14647/16642/20588 和 34344/33141/30263，NO1 两项为零。它们是观察副本损失，
  不能解释为 wire 坏帧数；锁存 miss 为零也不能抵消这些缺口。见
  `timing-comparison-r1.json`、`observation-scope-r1.json`、`retained-failures.json`。
- 许可证 9130400（grant epoch 14）已撤销，原始应答 `OK`、inactive 读回齐全。
  计时结束后的恢复前 STOP 在 NO3 应答超时；独立四板停止读回后，首次恢复仍遇到
  NO1 ARM map 拒绝（arm_result=5），并再次出现 NO3 STOP 应答超时。两次原件保留。
  另一次有界复位后仅重试恢复一次；本轮共有一次对照基线复位、一次恢复复位。
- `restore-r2` 已完成当前测量矩阵的四板 staging、运行期相位复核和最终 STOP，
  但严格启动仍失败，恢复 soak 在 NO1 记录 good 1795、transport bad 1，不能称为
  稳定恢复或校准接受。首次恢复审计因缺失停止成功字段报错，第二次编排误读了
  `restore-r1` 摘要；错误日志不改写，`final-restoration-audit-r1.json` 只读核对真实
  `restore-r2` 原件，确认当前相位已应用、四板 armed/enabled/adapter_started 为零。
- 文档回归 18 项通过；本切片软件/实链等价验证通过，硬件验收结论为 PARTIAL，
  长期目标 active。先处理当前 SCK/ARM/STOP/恢复坏帧失败，再推进 RX handoff 的
  不可变输入与有界提交；不能将 live ring copy 直接跨拍。六节点、正式 RAM、
  blackout、同圈交换与特等席保全门禁保持。主控复核、分离提交和证据封存见
  `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-039 - overlay DMA 描述符异步绑定

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-overlay-dma-bind/`。基线提交
  `aeaf416339972bcb2a37dcba210e338bc8749f29`，registry/C11 状态不变。
- 物理 owner 在 ARM 初始 PASS 绑定时，将已准入资源的控制字和地址冻结到
  `tdma_flight_overlay_binding_t`。Core0 在既有不可变输入与 inactive pool 租约内，
  用 `tdma_flight_overlay_bind_plan()` 校验并转换全部 DMA 描述符；仅写计划 SRAM，
  不访问外设。READY 后 Core1 继续核对 epoch/map/alignment、大小、local slot、
  terminal PC 与空闲池，赋予非零 generation 并发布后继指针，不再逐项转换描述符。
- 选择证据 → 数据 → loader 重启的顺序保持。新版本未就绪时旧计划继续；grant 仍先
  收割 pending selection，成功接受才更新 engine 事实。STOP 取消与后台 ACK 仍先于
  计划池/资源退休；同步后端复用同一 binder，存在后台写者时拒绝同步绑定。
  DMA selected generation 只证明计划内存可退休，不能解释为 SENT 或线路完成。
- 相关 Python 集成 120 项、真实物理 DMA/RX 回归 32 项和真实 adapter C 测试通过。
  已绑定描述符的数据流与原计划逐字相同，并通过移位/重定位 PIO 模型；无效 token、
  offset/count、端点、generation、terminal PC 及重复绑定拒绝不修改计划。DMA 总线
  模型覆盖 CPU 缺席、描述符空隙、池退休、generation 回绕、迟到写与 STOP；adapter
  覆盖暂停 Core0、源槽复用、旧 map/epoch 与取消。host 模型不替代硅上时序。
- 扩大检查暴露旧 RX 对齐夹具未同步 persona 字/字节接口，已修复测试模型，生产 RX
  未改；首次失败保留在 `dma-tests-r1`，复验见 `dma-tests-r2`。构建审计首次寻找已内联
  的 worker 符号失败，改为记录真实链接的 build/service 符号，固件未改；两次命令保留。
- Release A/B/Boot 为 `20260912205804`，源码指纹
  `1a802cf4eb4c734aaf6fc1ecc5f572cb1525844b3fee0c574f5940a35555660f`，源码文件 1022。
  六节点容量与生成 PIO 相同；模板新增 36 B 静态占用，A/B link free 各 4848 B，
  正式 RAM 仍 FAIL。源码/实链分支与边界见 `source-checkpoint-r1.json`、
  `source-review-r1.json`、`binding-boundary-review.json`。
- 当前源码四板 OTA/P3 完成，OTA 约 90.844 s，凭证 `passed=true`、
  `strict_gates_passed=false`。粗 CLK 拓扑读回不一致、严格启动屏障超时保留。
  STOP 应答成功，但 NO1 从 P3 最后快照至停止读回之间，TX timeout、origin DATA
  timeout/recovery 各增加 4，last_error 为 `TX_BUSY`；不能定位事件发生在运行或
  停止的具体边界，也不能归因于本次修改。一次有界复位仅恢复对照条件，不代表修复。
- 对照使用相同 RX14 旧诊断行、TX 相位、250 MHz 和负载 mask；逐样本核对配置，
  不作本轮校准接受。普通首末跨度 25.194 s（7 样本），自主 86.501 s（19 样本）；
  有效收帧为 6087/5987/6124/6089 和 41316/33904/32721/30491。两个窗口的
  transport bad 与已检查物理故障增量均为零，不能外推到 P3/停止期间或逐圈无损。
- 三从站普通窗口 overlay prepare 增量 5995/6113/6068，selected generation 增量
  同值；自主 prepare 增量 24370/24865/26078，selected 增量 24369/24864/26079。
  说明新后台绑定计划实际反复发布和选择，首末 pending 状态可使两类增量相差一；
  这些计数仍不能证明同圈多 owner 交换、逐帧 completion 或特等时间戳保全。
- 下表为自主窗口完整 profile 峰值及**同条峰值记录**中的 overlay 子项，单位 µs。
  不同分支、XIP/IRQ 和窗口变化仍存在，不能把完整降幅全部归因于描述符绑定迁移。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版 overlay | 本版 overlay |
|---|---:|---:|---:|---:|
| NO1 | 891.128 | 864.488 | 0 | 0 |
| NO2 | 945.096 | 839.368 | 181.836 | 114.064 |
| NO3 | 944.956 | 871.176 | 173.876 | 123.172 |
| NO4 | 942.132 | 872.328 | 173.596 | 99.896 |

- 普通完整峰值 1258.304/854.748/893.900/853.992 µs，尚未一致改善；自主完整
  SCHEDULE 超限增量 52317/57602/58011/55417。完整 380 µs 门禁仍失败，不能仅以
  自主完整峰值下降或局部 overlay 下降宣布 WCET 通过。profile 不含 recorder 发布，
  SCHEDULE 独立复核；嵌套阶段不可累加，稀疏 last 与完整峰值中的子项不可混用。
- 普通 RX ring overrun/observation drop 无增长；自主 NO2/NO3/NO4 分别增加
  15492/17402/21257 和 33890/32678/30452，NO1 两项为零。解析副本丢失不是
  wire 坏帧数，transport good 不能证明时间戳无损。见 `timing-comparison-r1.json`、
  `observation-scope-r1.json`、`retained-failures.json`。
- 临时许可证 9130390（grant epoch 14）已按原始应答和 inactive 读回撤销；本轮 P3
  派生矩阵已实际恢复，恢复诊断 soak 通过，四板 armed/enabled/adapter_started 为零。
  严格启动仍失败；当前切片 PARTIAL，长期目标 active。下一切片继续削减 RX handoff
  和 owner 提交成本，先取得不可变输入并保持帧/latch/generation 绑定，不能把 live ring
  copy 直接跨拍。380 µs、六节点、正式 RAM、blackout、同圈交换与特等席保全门禁保持；
  主控复核、分离提交与封存见 `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-038 - 私有 RX 头检查与 DMA 保留期限核验

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-rx-private-header/`。基线提交
  `854ab8121bc1b214327acb582976fd4dc6c11559`，registry/C11 状态不变。
- 已将 async RX 的物理头和 transport 固定头检查移到现有私有帧：先由
  `tdma_rx_scan_locate()` 核验几何、长度与已完成区间，单次归一化复制后检查 DMA
  observation epoch、计数及覆盖，再检查同一副本的完整固定头谓词。拒绝不推进
  接收 cursor、alignment 或成功事实；重新发现沿原工位推进，CRC 仍由原解析工位
  校验。Core0 不获得 live ring/硬件指针，wire、旧后端与唯一 owner 语义保持。
- 源码绑定的模拟 DMA 刺激改在实际 copy 入口触发，避免原来依赖 pre-copy header
  的测试钩子失效。RX 回归 28 项、相关集成 111 项与文档测试 18 项通过；新场景覆盖
  固定头 11 字节逐项损坏及有效对照，在 8 位相、2 种 ISR 方向下验证拒绝/接受。
  有效候选只读 frame_words 加非零位移的额外原始字；覆盖/epoch 拒绝保留 copy
  计时且不执行 header 检查。host DMA 为受控替身，不能替代硅上时序。
- `capture-boundary-audit-r1.json` 绑定 ring、帧长、编译 baud、调度符号及前版实测
  clock。连续 10 MHz 字节流下，1024 原始字 ring 约 819.2 µs 覆盖一轮；296 字帧
  刚完成后首部最多再保留约 582.4 µs，旧候选更短，均不能保证跨 1 ms 调度周期。
  这是连续线速上界模型，不是新测得的 CS 间隔；不把整个物理飞行时间扣出 CPU
  预算。因此先保全完整私有输入，再异步/分步处理，不能直接把 live copy 切到下一拍。
- Release A/B/Boot 为 `20260912202605`，源码指纹
  `2b2f09673ef1ec100d29a7838fb4d2c6d970cdfa5641289ae5fdbd70e6e5de5a`，源码文件 1022。
  六节点容量与生成 PIO 相同，A/B link free 各 4884 B，本轮新增静态 RAM 为零；
  正式 RAM 仍 FAIL。见 `source-checkpoint-r1.json`、`source-review-r1.json`。
- 当前源码四板 OTA/P3 完成，OTA 约 90.141 s，凭证 `passed=true`、
  `strict_gates_passed=false`。粗 CLK 校准出现拓扑读回不一致，严格启动屏障超时；
  coded marker 无拒绝、process-image 诊断 soak 通过。本轮 STOP 应答均成功，但
  NO1 在 P3 最后物理快照至停止读回之间，TX timeout、origin DATA timeout/recovery
  各增加 7，停止读回 last_error 为 `TX_BUSY`。现有证据不能定位在运行还是停止期间，
  也不能归因于本次修改；原件见 `lifecycle-audit-r1.json`。一次有界复位成功，不能
  以其后的无故障窗口清除这些失败，也不宣称修复前序 ARM map 或 STOP 应答缺陷。
- 对照使用与前版相同的 RX14 旧诊断行，逐样本复核 TX/RX 相位、负载与 clock，
  不作本轮校准接受。普通首末跨度 25.631 s（7 样本），自主 86.319 s（19 样本）；
  有效收帧分别为 6194/6108/6171/6210 和 41452/33684/32718/30143。两个窗口的
  transport bad 与已检查物理故障增量均为零，不能外推到 P3/停止期间或逐圈无损。
- 下表是自主窗口完整 profile 峰值，以及**同条峰值记录**中的固定头子项，单位 µs；
  它不是各子项独立最大值。NO1 此条记录没有走 follower/legacy 的异步取帧分项。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版头检查 | 本版头检查 |
|---|---:|---:|---:|---:|
| NO1 | 927.788 | 891.128 | 0 | 0 |
| NO2 | 942.964 | 945.096 | 41.404 | 9.444 |
| NO3 | 1041.824 | 944.956 | 88.308 | 4.600 |
| NO4 | 1072.412 | 942.132 | 95.720 | 10.280 |

- 普通完整峰值为 1233.076/851.060/891.148/859.772 µs；自主同记录的从站 RX
  handoff 仍为 441.140/491.412/427.744 µs，取帧分别为 224.392/269.648/242.404 µs。
  自主稀疏 last 头检查中位数 NO2/NO3 由 36.264/68.870 降至 3.640/3.676 µs；
  NO4 本轮为 4.134 µs，前版无该子项 last 样本，不能补成零值对照。窗口和分支不同，
  有限样本支持局部收益，不证明完整 WCET。自主完整 SCHEDULE 超限增量为
  50584/59187/61138/60744；profile 不含 recorder 发布，嵌套阶段不可累加。
- 普通 RX ring overrun/observation drop 无增长；自主 NO2/NO3/NO4 分别增加
  15951/17521/21075 和 33606/32668/30141，NO1 两项为零。观察丢失不是 wire
  坏帧数，transport good 不能证明时间戳保全。见 `timing-comparison-r1.json`、
  `observation-scope-r1.json`、`retained-failures.json`。
- 临时许可证 9130380（grant epoch 14）已由原始应答与读回确认撤销；本轮 P3 派生
  矩阵已实际恢复，恢复诊断 soak 通过，四板最终 armed/enabled/adapter_started 为零，
  严格启动仍失败。下一切片继续收敛 RX 交接与 owner 内部工作，先绑定完整 generation
  和帧/时间戳，再分步推进私有数据；不能把耗时藏入另一个无预算 action。现有
  380 µs 门限、六节点与后续项顺序保持，service blackout、同圈 owner 更新、逐圈
  CRC/sequence、特等时间戳和正式 RAM 仍须独立验收。切片 PARTIAL，长期目标 active；
  主控复核与分离提交封存见 `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-037 - 训练仲裁占用与 Core1 有界完成发布

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-training-publication/`。基线提交
  `e08a0d319c4e9c62e0f50ce516ac0533be504824`；本轮不变更 registry/C11 状态。
- Resource Arbiter 在 Core0 原临界区内保留训练占用，再调用 TDMA owner 的有界软件
  intent 发布；失败保留此前占用。Core1 改为原子字段与屏障保护的单 writer 完成事实，
  仲裁侧一次读取，旧值、读竞争或 generation 不符都不提前释放。新增
  `train_owner_sequence` 仅由 owner 执行/成功取消推进，配置侧 accepted 重置不再
  充当硬件终态；STOP pending、配置未应用及仍 enabled 的 coarse-training SM 均
  阻止释放。重复仲裁初始化保留请求，稳定语义已写入 TDMA 域架构。
- FlashTransaction 的策略检查和 ACQUIRE 是不同 action，最终 Flash 资源取得现在
  也在仲裁锁内检查训练占用；拒绝记录 holder `TDMA_CLOCK_TRAINING`。首版构建完成
  后复核发现此窗口，补检查后以最终版本重新测试/构建；首版未部署且原件保留，见
  `variant-history.json`。wire、PIO 程序、clock 源与编译容量未改变。
- 源码提取的真实 owner/service/物理终态查询与真实 runtime/arbiter 通过 17 个主机
  交错场景，覆盖旧完成覆盖、发布途中完成、拒绝保留旧请求、STOP、重置、回绕、
  快照失败、兼容标志、仍运行的 SM 以及 Flash 最终取得资源。旧 runtime/arbiter、
  Flash 普通/validation/OTA journal/journal 变体及 138 项集成回归通过，文档检查器
  18 项通过。主机物理回调/PIO 是受控替身，不能替代板端并发或时序证明。
- 最终 Release A/B/Boot 为 `20260912195528`，源码指纹
  `aba4f6f37ae6672c450ba5416ea6731928eca59e135255e998198b9a4c95267c`，源码文件 1022。
  A/B link free 各 4884 B，比前版减少 20 B，正式 RAM 仍 FAIL。实链检查发布路径只有
  有界读取、屏障和写入，无共享 OSAL 锁、重试环、RMW atomic helper 或整份快照复制；
  不将指令审查提升为完整 WCET 证明。见 `source-review-r1.json`、`assembly-review-r1.json`。
- 当前源码四板 OTA/P3 完成，OTA 耗时约 91.266 s；凭证 `passed=true`、
  `strict_gates_passed=false`。除严格启动屏障超时外，coded marker 的 NO1 reference
  试验出现一次 NO3 ARM `FLIGHT_MAP_REJECTED`；P3 process-image 诊断 soak 通过。
  计时前 NO4 STOP 应答超时，两次独立读回均确认四板 armed/enabled/adapter_started
  为零、物理 last_error 为零，随后执行一次有界复位。原拒绝和超时不改判，见
  `lifecycle-failures-r1.json`；本轮不宣称修复一般 ARM map 或 STOP 应答问题。
- 对照仍采用前版 RX14 旧诊断行，逐样本核验相位、clock、负载掩码和 WCET；它仅用于
  成本比较，不充当当前校准接受。普通首末跨度 22.217 s，自主 90.692 s；有效收帧
  分别为 5394/5362/5451/5431 与 42793/34887/33680/30603。两个窗口 transport bad
  和已检查物理故障增量均为零。临时许可证 9130370（epoch 14）已原始应答/读回确认
  撤销；当前 P3 派生矩阵已实际恢复并四板 STOP，恢复诊断 soak 通过，严格启动仍失败。
- 下表为自主窗口的完整 profile 峰值及**同条峰值记录**中的训练发布子项，单位 µs；
  子项不是独立最大值，两个 build 的窗口长短和样本数也不同。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版训练发布 | 本版训练发布 |
|---|---:|---:|---:|---:|
| NO1 | 906.880 | 927.788 | 29.204 | 16.892 |
| NO2 | 986.032 | 942.964 | 27.976 | 12.752 |
| NO3 | 1108.444 | 1041.824 | 23.276 | 19.864 |
| NO4 | 1074.636 | 1072.412 | 20.776 | 14.700 |

- 自主稀疏 last 样本的训练发布中位数由 18.228/10.120/10.200/12.160 µs 变为
  6.726/7.812/9.428/9.420 µs；普通与自主的该中位数均下降。完整自主峰值三板下降、
  NO1 上升；普通完整峰值为 1224.440/892.336/952.392/888.748 µs。有限样本支持
  局部收益，不证明稳定整体收益；不能把 XIP、IRQ、分支和共享访问变化全部归因于
  锁移除。profile 不含 recorder 发布，完整 `SCHEDULE` 超限另查；本轮自主超限增量
  为 51436/60567/61235/60192，完整 WCET 仍 FAIL，见 `timing-comparison-r1.json`。
- 自主 NO2/NO3/NO4 的 RX ring overrun 增量为 17442/19419/22887，观察副本 drop
  为 34888/33665/30604；NO1 两项为零。transport good 不证明逐圈无损或时间戳保全，
  这些观察损失也不是 wire 坏帧数。原件边界见 `observation-scope-r1.json`。
- 下一切片聚焦仍超预算的 RX handoff 和 owner 内部工作；保持 generation、取消、
  池归属与真实完成语义，不把耗时藏进另一个无预算 action。现有 phase 门限、六节点
  配置与后续项顺序保持；service blackout、多 owner 同圈更新、逐圈 CRC/sequence
  和特等时间戳保全仍须独立验收。切片 PARTIAL，长期目标 active；主控复核及分离
  提交封存见 `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-036 - 训练发布竞态回放与完整峰值成本核算

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-training-gate-audit/`。基线为
  `8147e6b9eb5b05cee579ff1bdd14797f3ae4f83f`，源码指纹继续为
  `64b266f3dcdea28162073b5662f3e7e253ea701a1f8d51d4d11078dfd9ab0f91`。前序切片已
  完成分离提交和 336 份证据校验；本轮只运行主机诊断与既有计时核算，没有新固件、
  板端命令、许可证或 P3，不把前序硬件证据写成本轮新验收。
- 从当前源码逐字提取 owner 训练入口、gate 发布、service facade 和物理快照 getter，
  与真实 runtime/Resource Arbiter 一起编译；OSAL 与物理回调使用可控主机替身。
  `replay-results-r1.json` 的 9 个场景覆盖不变值发布、旧值覆盖、命令可见顺序、活跃、
  完成、拒绝、STOP 失败/成功与快照读取失败；测试成功表示预期行为被复现，未表示
  固件缺陷修复或真实板上已发生并发 Flash 操作。
- 已复现：Core1 算出 inactive 后、进入共享锁前，Core0 可以提交训练并发布 true；
  Core1 随后写入旧 false。此时 command/accepted 为 1/0，真实 arbiter 的 eligibility
  和 admission 接口均允许 OTA。命令 release 也确实先于 true 发布，与原注释承诺的
  顺序不同；实际调用者的其他策略与 Core0 串行化可能进一步限制操作，不能外推硬件
  损坏或线上发生率。注入 odd guard 后清占用仅证明失败返回语义，未证明该状态在
  实际唯一 owner 边界可达。
- `stop-boundary-results-r1.json` 补充两个真实 runtime 控制路径：配置 STOP 即更新
  accepted 序列，而 adapter 仍 started；物理 STOP 被替身拒绝后，command/accepted
  仍相等且 stop_pending 为真。因此序列相等不能独立作为终态确认。SCPI 与后台准备
  位于不同 Core0 任务，迁移发布到 Core0 后仍需请求与同步的显式串行化。
- 每拍不变值发布回放 100 次，确实进入 OSAL 100 次；目标多核 OSAL 使用共享
  `spin_lock_blocking()`。这只证明路径存在，不证明具体锁等待耗时。当前 build 的
  普通/自主计时原件逐文件校验后，按**同一完整峰值记录**扣除训练 gate 子项；
  `budget-audit-r1.json` 保留时钟拍数、序列、嵌套关系和原件 SHA。

| 自主节点 | 完整峰值 µs | 其中训练发布 µs | 扣除此项后的其余已记录工作 µs |
|---|---:|---:|---:|
| NO1 | 906.880 | 29.204 | 877.676 |
| NO2 | 986.032 | 27.976 | 958.056 |
| NO3 | 1108.444 | 23.276 | 1085.168 |
| NO4 | 1074.636 | 20.776 | 1053.860 |

- 普通模式扣除此项后的其余工作为 1229.368/930.908/984.204/972.672 µs，同样超出
  `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`。这是原记录成本核算，不是新实现性能预测
  或 WCET 下界；XIP/IRQ/竞争随实现改变，物理飞行与 CPU 工作也可重叠。不得再将
  物理飞行时间从异步 CPU 总预算完整扣除，或把嵌套子项重复相加。
- 下一实现边界见 `design-findings.json`：命令可消费前建立占用，Core1 只发布有界、
  generation 绑定的执行/完成证据，仲裁侧保留 pending、拒绝、旧证据、重置及真实
  STOP 的语义；移除每拍共享锁前须证明上述交错均不提前释放。完整性能主线继续
  拆分 owner/runtime/adapter RX handoff 工作，不能只做训练发布局部优化。
- 本轮为 AUDIT_COMPLETE，固件修复、严格启动、完整 WCET、正式 RAM、观察无损、
  service blackout、同圈多 owner 更新与特等时间戳保全仍开放；长期目标 active。
  未变更稳定架构语义或 registry/C11 状态，文档门禁与封存见 `review-r1.json`、
  `commit-proof.json`。

### TDMA-PROGRESS-20260913-035 - 公共时钟精确整数换算与物理 RX 成本对照

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-clock-conversion/`。
- 实链确认物理 RX 提取、RX/TX latch rearm 调用 `vdc_timestamp_clock_now_ns()`，
  原实现每次进行两次通用无符号长除法。`vdc_timestamp_clock_ticks_to_ns()` 现在检查
  缓存 frequency 与向上取整 resolution 的乘积；只有精确组成一秒时才直接相乘，否则
  仍使用原商余数公式。保留零频率处理、向下取整与无符号结果回绕；timer 源、原始拍数
  读取、epoch、初始化和 Core1 schedule 未改变，稳定语义写入 VDC 域架构。
- 真实 C 设备分支通过模拟 clock/timer 运行，与 Python 无界整数参考对照。覆盖全部
  整数纳秒周期频率、随机及边界非整数频率、零频率、秒边界、原始计数器字边界、
  纳秒输出回绕和重复初始化不重置 epoch。新增 2 项算术测试及原 138 项集成测试通过，
  文档检查器 18 项通过；A/B 实链确认整数路径跳到 UMULL/MLA/返回，避开两次除法，
  非整数频率和冷初始化保留除法。证据见 `clock-tests-r1.*`、`assembly-review-r1.json`。
- Release A/B/Boot build 为 `20260912185952`，源码指纹为
  `64b266f3dcdea28162073b5662f3e7e253ea701a1f8d51d4d11078dfd9ab0f91`。
  编译容量仍为 `PROJECT_NODE_CAPACITY=6`，A/B link free 各 4904 B，无新增静态 RAM，
  PIO 生成头与前版相同。四板真实 P3 `run --tdma-only` 完成，凭证 `passed=true`、
  `strict_gates_passed=false`；严格启动屏障超时保留，诊断继续不作为产品通过。
- 计时前四板 STOP 应答和停止读回正常，物理错误均为零；一次有记录的软件复位匹配
  progress034 起点，之后四板正常 ARM。使用同一 RX14 旧诊断行，逐样本核验 TX/RX
  相位、clock、WCET 与负载掩码；该行不作为本次有效采样窗口或校准接受。
  普通首末样本跨度为 22.038 s，自主为 87.539 s；有效收帧分别为
  5336/5325/5382/5328 与 41704/33007/32322/30103，transport bad 与所检查 physical
  fault 均无增长。trial 9130350 / epoch 14 已撤销；本次 P3 新矩阵已实际恢复，四板
  最终 STOP，恢复诊断 soak 通过，严格启动超时继续保留。
- 下表为自主窗口的完整 profile 峰值及该条峰值记录中的 RX_CLOCK/RX_LATCH 子项，
  单位 µs。两版是独立窗口；子项不是单函数微基准或独立 WCET，不能把两版不同分支、
  代码布局、共享总线或 IRQ 干扰下的差值全部归因于换算实现。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版 clock / latch | 本版 clock / latch |
|---|---:|---:|---:|---:|
| NO1 | 900.652 | 906.880 | 0 / 0 | 0 / 0 |
| NO2 | 1029.816 | 986.032 | 30.076 / 56.068 | 13.360 / 26.896 |
| NO3 | 1020.324 | 1108.444 | 17.920 / 27.720 | 23.568 / 50.180 |
| NO4 | 1091.112 | 1074.636 | 23.352 / 59.196 | 19.084 / 31.076 |

- 自主完整峰值两板下降、两板上升；普通完整峰值为
  1288.272/948.256/1000.956/995.680 µs，也未全部改善。稀疏 last-clock 样本很少且
  中位数未一致下降，不能宣布稳定的整体收益。自主 `SCHEDULE` overrun 增量为
  58628/60499/61602/62384，完整 WCET 与正式 RAM 仍 FAIL。
- 自主 NO2/NO3/NO4 的 `rx_ring_overrun_count` 增量为 18252/19481/21848，
  `rx_observation_drop_count` 为 33002/32402/30081，NO1 两项均无增长。已接收帧的
  CRC 正确性不证明逐圈观察无损；这些计数表示观察覆盖/丢弃，不能换算成 wire 坏帧数。
  当前/前版原件对照与撤销读回见 `observation-scope-r1.json`。
- 下一步审计每拍训练 gate 发布的全部读写者、pending command 与初始化边界：当前
  `tdma_runtime_owner_update_training_gate()` 复制训练快照后进入 Resource Arbiter
  的共享 OSAL 临界区，多核入口为 `spin_lock_blocking()`；尚未隔离其等待时间。
  不能通过仅缓存 Core1 当前状态破坏命令侧的提前准入投影，见
  `next-boundary-inspection.json`。本轮不修改该路径。
- 预算保持 `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`，节点容量后续项后置；没有新增
  service blackout、自主波形、同圈多 owner 更新或特等时间戳保全证明。切片 PARTIAL，
  长期目标保持 active；审核与分离提交封存入口为 `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-034 - 无新版本 TX 快速复用与完整阶段对照

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数字为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-overlay-reuse/`。
- Core1 在物理 grant 服务 pending selection 后，核对上次成功准备的 epoch、active
  map、local slot 和 map generation。FIFO 空且 active 槽完整 generation/sequence
  匹配时，直接保留旧镜像，跳过重复 layout、hop 与 TX view 准备；复用证据与原 acquire
  空队列路径一致。排队或损坏描述符、变化版本仍走完整获取与校验，STOP 取消和池归还
  顺序保持。该切片不新增 RAM、PIO/DMA 资源或新的 owner。
- 真实 C adapter/FIFO 两套测试通过：重复复用与原 acquire 的完整 FIFO 状态逐次相同；
  覆盖新发布、损坏描述符、已释放槽、map 发布中、物理 pending、后台暂停及 STOP/rearm。
  Python 集成 138 项、文档检查器测试 18 项通过。A/B 反汇编确认物理 grant 在前，复用
  成功直接返回，失败回完整路径；源码与实链审核见 `assembly-review-r1.json`。
- Release A/B/Boot build 为 `20260912183136`，源码指纹为
  `743487d0290a8d32461574004e3c3fca8fc350b0b5a62509e261e8e90a01db65`。
  编译容量保持 `PROJECT_NODE_CAPACITY=6`，A/B link free 各 4904 B，PIO 生成头与前版
  相同。四板真实 P3 `run --tdma-only` 完成，凭证 `passed=true`、
  `strict_gates_passed=false`；本次编码 marker 未列为失败，严格启动屏障仍超时。
- 计时前 STOP 的 NO4 应答超时，首个 reset helper 拒绝执行复位；两次独立快照确认
  四板 armed/ring_enabled/ring_adapter_started 均为零，NO1 留有
  `TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY=5`。保留原始失败后执行原定的一次软件复位，
  核对 build/UID，再成功 ARM。复位用于匹配 progress033 起点，不关闭 STOP 应答及
  前序 ARM 聚合拒绝缺陷，见 `reset-recovery-r1/r2.json`、`retained-failures.json`。
- 普通/自主对照沿用 progress033 的 RX14 旧诊断行，TX 为 15/14/15/15；每组读回均核验。
  该行只作比较，不作为本次有效采样窗口。样本首末跨度分别为 21.630 s / 87.424 s，
  普通有效收帧 5361/5293/5352/5319，自主 41983/33526/32266/30476；各板 transport bad
  与已检查 physical fault 均无增长。trial 9130340 / epoch 14 已撤销，本次 P3 新矩阵
  已实际恢复，并确认四板 STOP；恢复诊断 soak 通过，严格启动超时仍保留。
- 已接收帧零 CRC 错误不代表观察无损。自主窗口 NO2/NO3/NO4 的
  `rx_ring_overrun_count` 增量为 18422/19260/22030，`rx_observation_drop_count` 增量为
  33522/32185/30469；前版对应覆盖增量为 17362/18961/22246，观察丢弃为
  33958/33076/30895，两个窗口长度不同。NO1 两项均无增长。这些是观察链路覆盖与
  副本丢弃证据，不能推成同等数量的 wire 坏帧，也不能据 transport bad 无增长宣布
  逐圈保全通过；补充原件审核见 `observation-scope-r1.json`。
- 下表是两版自主窗口的完整 profile 峰值及该峰值记录中的 overlay 子项，单位 µs。
  子项不是独立 overlay WCET，也没有区分当拍是否命中快速复用；不同版本的独立窗口
  不能排除代码布局、IRQ/共享访问或工作分支差异。原始记录与 SCHEDULE 分别审计。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版峰值内 overlay | 本版峰值内 overlay |
|---|---:|---:|---:|---:|
| NO1 | 920.296 | 900.652 | 0 | 0 |
| NO2 | 969.008 | 1029.816 | 174.736 | 133.984 |
| NO3 | 1048.148 | 1020.324 | 196.032 | 147.916 |
| NO4 | 1113.092 | 1091.112 | 176.760 | 163.872 |

- 自主完整峰值三板降低、NO2 上升；普通完整峰值为
  1303.676/952.200/996.052/1023.772 µs，也未一致改善。自主 `SCHEDULE` overrun
  增量为 58948/61046/62053/63013；窗口长度不同，不能把增量直接作改善率。稀疏
  last 样本的 overlay 中位数同样有升有降，不能宣布稳定的整体性能收益。
- 完整 WCET、正式 RAM 仍 FAIL，门限保持 `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`。
  下一步按同一条完整记录区分 RX handoff、runtime 事实发布与运行干扰；RX_CAPTURE
  已包含在 RX_HANDOFF 中，不能重复相加。没有新增 service blackout、自主波形、同圈
  多 owner 更新或特等时间戳保全证据；切片 PARTIAL，长期目标保持 active，节点容量
  后续项后置。审核与分离提交封存入口为 `review-r1.json`、`commit-proof.json`。

### TDMA-PROGRESS-20260913-033 - persona 专用 RX 复制与显式 DMA 字读取

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数值为实验快照，非事实源；
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-rx-copy-persona/`。
- `tdma_pio_spi_phys_rx_ring_copy()` 在既有 Core1 owner 同步边界内一次选择 persona，
  普通与反转方向分别使用对齐/错位循环；反转通过共用 helper 内联 RBIT。复制后
  observation epoch/覆盖复验、STOP/配置取消、latch 因果边界与原始 wire 路径保持。
  ARM/PIO/DMA 资源分配与正式预算未改变。
- 反汇编复核抓到旧 word reader 未显式 volatile：首版出现 byte-width load 和多余的
  入口读取。该候选 P3 在 build 阶段主动终止，未 OTA；`p3-command-r1.*`、首版镜像
  和 checkpoint 保留。最终在 word reader 使用 `const volatile uint32_t *`，实链 A/B
  确认模式判断只在入口，DMA 读取为 LDR.W，归一化循环无外部调用。
- `rx-tests-r2` 与 `final-integration-tests-r1` 合计 147 项不同 Python case 通过；真实 C
  差分覆盖 320 种组合，按独立 wire bit 预期验证两种位序、全部 bit shift、不同长度、
  SRAM/sequence 回绕、未对齐目标哨兵及精确读取次数。保留推进中 DMA 覆盖/epoch
  和异步取消测试。第一次集成夹具漏引 helper 的 44 项 setup error 保留，修正后重测。
- Release A/B/Boot 为 build `20260912180157`，源码指纹
  `c385db6faa778e1804f3498279792886edfb5813ab9167d751e9f0ea45eedf3e`；编译容量仍为
  `PROJECT_NODE_CAPACITY=6`，A/B link free 各 4904 B，未增加链接 RAM。PIO 生成头与
  前序版本逐字节相同。四板真实 P3 `run --tdma-only` 完成，诊断凭证 `passed=true`、
  `strict_gates_passed=false`；编码 marker 拒绝及短帧启动屏障超时保留。
- RX14 对照准备先遇 NO4 `FLIGHT_MAP_REJECTED=5`；一次有界重试再遇 NO3
  `RUNTIME_CONFIG_REJECTED=8`。两次均未进入自主试验，均实际恢复本次矩阵并四板 STOP。
  原始拒绝快照显示 stopped、physical last_error 为零、process configured/inactive。
  随后一次软件复位复核相同 build/UID，再成功四板 ARM；不能以该成功消除原拒绝事实。
  具体 predicate/跨核 generation 仍须定位，见 `arm-boundary-inspection.json`。
- 复位后的 RX14 对照使用与 progress031 相同的旧诊断矩阵行；发送相位为
  15/14/15/15，RX/TX 读回逐样本核验。这用于比较两版复制实现，不作为本次有效
  校准窗口。普通样本首末跨度 21.823 s，自主首末跨度 88.920 s；名义采集时长包括
  第一组查询，不能直接当作计数差分窗口。自主模式有效收帧为
  42426/34025/33122/30914，各板 transport bad 与已检查 physical fault 均无增长。
  临时许可证已撤销，恢复本次 P3 矩阵并确认四板 STOP；原始 SCPI/计时/阶段记录在
  `autonomous-r3.json`、`timing-r3/`、`restore-r3/`，比较入口为 `timing-comparison-r1.json`。
- 以下是各版自主窗口中**完整 profile 峰值所对应的同一条记录**，单位 µs；copy 列
  是该条记录的子项，不是独立复制 WCET。两版是分开采集，不能将减少量解释为
  排除代码布局、IRQ/共享总线干扰后的纯指令收益。

| 节点 | 前版完整峰值 | 本版完整峰值 | 前版峰值内 copy | 本版峰值内 copy |
|---|---:|---:|---:|---:|
| NO1 | 894.100 | 920.296 | 0（该路径无 ring copy） | 0 |
| NO2 | 1024.856 | 969.008 | 146.584 | 89.476 |
| NO3 | 1096.108 | 1048.148 | 245.244 | 127.664 |
| NO4 | 1113.456 | 1113.092 | 212.052 | 157.856 |

- 自主从站 copy 子项下降，完整峰值改善较小且 NO1 上升；普通完整峰值为
  1231.076/928.396/949.336/1025.940 µs，也未统一改善。自主 `SCHEDULE` overrun
  增量为 57101/62188/63239/62719，完整 WCET 仍 FAIL；正式 RAM 仍 FAIL。
  门禁保持 `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`，不因本次局部收益放宽。
- 下一步优先核验无新版本时 overlay 准入与 RX handoff 的剩余成本，并保留 pending
  硬件服务、FIFO 消费/释放顺序和配置失效检查；ARM 聚合拒绝须细化到实际谓词与
  owner 边界。暂未新增自主 waveform、service blackout、同圈多 owner 更新或特等
  时间戳保全证据。切片为 PARTIAL，`TDMA-FLIGHT-002` 持续推进，节点容量后续项后置。

### TDMA-PROGRESS-20260913-032 - 当前测量基线上的独立 RX 窗口扫描

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`、`TRN-ORIGIN-RX-01`。
  以下数值为实验快照，非事实源。证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-origin-rx-window/`。
- 基线提交 `de69869873f5a1a95a5ac94b7c25bb871933b573`，build 保持
  `20260912164001`、源码指纹
  `a34652c8546de0f77a1f0dd409953ad7e0209c2be6a37a8b74fd08dd36c894d6`。
  先复核上一切片 manifest 的 349 份原件、构建产物与 helper，再读取四板实际
  build、运行/物理状态并确认 STOP。本轮为有界硬件诊断、离线审阅及文档更新，
  未修改固件、PIO 或正式工具，未重新构建或以新 P3 凭证替代既有失败。
- `matrix-window-diagnostic-r1.json` 的链路 base 来自上一轮当前 P3 测量，
  generation 保持 1789231681；独立 RX 与 NO2 DATA 压力点显式标记为未接受的
  诊断扩展。完整矩阵共 192 行；选行通过现有 base/offset、编码范围和 re-arm
  检查，但这不是接收有效窗口的测量通过。全部压力组 TX phase 保持一致，
  仅 origin RX 分别取 15 / 14 / 13 / 12 / 10 / 8，并以 RX15 回切作控制。
- 初始基线观察 40.719 s，NO1 good 9928 / transport bad 0。首个压力组在 NO2
  ARM result 8 拒绝，原始错误和停止态快照保留；只有 physical 未 armed、无物理
  error、runtime 未启动且 map configured/inactive 的证据齐全时才允许一次
  STOP 后重试。重试不提升先前失败或替代正式恢复闭环。
- 固定 TX 的压力组结果如下；每行是独立有限窗口，不能相加为连续长稳记录。

  | origin RX phase | 观察时间 / s | NO1 good frame | NO1 transport bad |
  |---|---:|---:|---:|
  | 15，初次控制 | 41.687 | 9959 | 33 |
  | 14 | 40.000 | 9723 | 0 |
  | 13 | 40.188 | 9828 | 0 |
  | 12 | 40.453 | 9860 | 0 |
  | 10 | 40.515 | 9815 | 0 |
  | 8 | 40.031 | 9735 | 0 |
  | 15，最终回切 | 40.609 | 9666 | 66 |

  其余从站的 transport CRC 无增长，四板物理 fault 和 counter regression 均无
  增长。最终 RX15 回切的 NO1 出现一次接收健康 DOWN 判定及一次恢复：16.812 s
  样本的 receive state 为 2、failure streak 为 1，18.687 s 回到 state 1、streak 0；
  两次 runtime 的 enabled/adapter_started 均为真，不能据健康判定声称物理线路
  停止。其余窗口没有 down event。首次主控复核因此拒绝“全程无 down event”
  摘要，原失败和脚本保留，修正复核见 `review-r2.json`。
  已测的相邻点与回切反例支持将 RX12 作为后续内部候选，
  不插值未测选行，也不把离散相位差解释为已保证的模拟余量。
- `window-analysis-r1.json` 重新核对各原始 summary 的 build、实际 TX/RX phase、
  全部选行、失败重试与最后恢复。恢复使用当前原矩阵，8.391 s 内 NO1 good
  2106 / bad 0，四板实际 RX/TX 回到原共用基线并最终 STOP。所有严格 startup
  barrier 超时原样保留，诊断 soak 通过不提升 `passed/closed_loop_passed`。
- 后续成本候选的源码与镜像核验见 `copy-path-inspection-r1.json`。当前 A/B
  镜像都已有内联 RBIT，但 RX 复制循环仍逐字读取 persona 并分支，带 bit-shift
  的路径在分支前后重复计算部分字节结果。可验证的下一候选是在既有 Core1 owner
  边界内读取稳定 persona、选择专用归一化循环；必须保留 DMA volatile 读取、
  ring wrap、位对齐、epoch/覆盖复验和 STOP/配置边界。未改变实现，静态指令
  检查不提供性能收益承诺。overlay 的无新版本早退仍位于 grant/layout 之后，
  其调整另需核对 FIFO 消费顺序和 pending 硬件确认，不能直接跳过有副作用的步骤。
- 本轮保持 PARTIAL，`002F` 与长期目标继续 IN PROGRESS/active。正式 RAM、
  完整 WCET、同一拒收帧绑定、无 service 自主循环、多 owner 同圈更新和特等席
  逐圈保全仍开放；保持当前门限、节点容量后续项顺序和 registry/C11 状态。

### TDMA-PROGRESS-20260913-031 - Calibration 所有的独立 origin RX 相位与实板对照

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`、`TRN-ORIGIN-RX-01`。
  以下数值为实验快照，非事实源。证据根为
  `out/HardwareAcceptance/20260913/tdma-flight-origin-rx-phase/`。
- 基线提交 `8ef214eee7b5b973ff8dff07f12ab54badbb2def`；新增独立 capture offset/phase，
  沿 Calibration stopped staging → TDMA owner → ARM 物理配置传递。普通与自主
  origin 的 RX 使用独立参数，TX 与 follower 重定时保留原 DATA 参数。新增 PHYS
  读回、完整矩阵维度和 base/offset 一致性校验；省略参数或旧 payload 恢复原共用
  行为。版本化存储保存新字段，固定槽数与 Flash 分区不变。没有改动 PIO 指令布局、
  资源分区、运行期 owner 或正式预算；候选接口说明见 Calibration training plan。
- 软件：相关 Python 用例共 256 个通过，runtime、training store 与 service scheduler
  的真实 host C 编译运行通过。首次测试为 178 passed / 12 setup errors，失败来自旧
  payload 大小断言；区分新旧版本后容量组 17 passed，另有集成组 66 passed。
  前后容量组重复用例未重复计数；原始失败与修正后运行日志均保留。
- 构建与资源：Release A/B/Boot build `20260912164001`，当前源码指纹
  `a34652c8546de0f77a1f0dd409953ad7e0209c2be6a37a8b74fd08dd36c894d6`。
  build 与 Flash link contracts 通过；App A/B 链接剩余均 4904 B，比上一版多占
  264 B，正式 RAM 门禁仍失败。编译容量保持当前配置，节点容量后续项仍后置。
- 当前源码真实 P3 `p3-r1/` 完成四板 OTA、拓扑与物理训练流程；凭证
  `p3-receipt-r1.json` 为 `passed=true/strict_gates_passed=false`。process-image
  原始 summary 的 `passed/closed_loop_passed/realtime_gate_passed` 均 false，
  `diagnostic_passed/diagnostic_continue` 均 true；保留启动屏障超时，不提升为产品通过。
  本轮新矩阵 `matrix-strict-r1.json` 来自当前 P3 测量，generation 为 1789231681。
  RX 对照矩阵另以旧已封存矩阵扩展完整搜索维度，明确 `passed=false`、diagnostic
  replay；没有把人工候选选行当作新校准有效窗口。
- 首轮普通模式 `rx-comparison-r1.json`：基线 RX/TX 共用参数时，NO1 在 39.703 s
  窗口取得 good 9707 / transport bad 0。只把 NO2 DATA 改为 14 后，NO1 在
  38.782 s 为 good 9171 / bad 49；保持全部 TX 参数，仅将 origin RX 从 15 改为
  14，38.406 s 为 good 9380 / bad 0。各窗口从站 transport CRC 均无增长，四板
  down event、物理 fault growth 与 counter regression 均无增长。回切组在 NO3
  ARM result 5 拒绝，未取得 soak，首轮往返对照不完整。result 5 对应 flight map
  准入拒绝，具体瞬态原因未确认；原始失败不能由后续复验覆盖。
- 第二轮固定 TX 的 RX15 → RX14 → RX15 对照：NO1 分别为
  38.921 s / good 9329 / bad 25、119.094 s / good 29020 / bad 0、
  41.391 s / good 9656 / bad 87。各从站仍未见 transport CRC 增长。该往返关系
  进一步支持接收采样相位的影响，不能仅凭有限零错误把 RX14 固化为产品参数。
- 当前源码普通模式波形 `waveform-analysis-r1.json`：两份 capture 均由下载的原始
  segment 逐字重建，参考帧 sequence 30928 / 12016 的四个活动 mailbox owner 与
  CRC16 均通过。固定参考 decode 下，离散同值区间相同；它不是模拟 setup/hold
  下界，也没有绑定固件拒收的同一帧副本。RX14 / RX15 capture-export 区间的
  transport bad 分别为 0 / 127；该区间不包含之后的串口下载，不能与 soak 数值
  直接相加。第二轮最终恢复在 NO4 ARM result 8（runtime config rejected）失败，
  对照已完成但首个恢复失败原件保留；独立恢复使用同一当前矩阵重新验证。
  `restoration-verification-r2.json` 已核对 staging、运行中实际 phase 和最终 STOP；
  恢复后的健康 soak 不改变严格启动超时结论。
- 命令负测 `stage-rejections-r1.json`：NO1 停止态下，offset-only、格式错误、
  offset/phase 不一致与不可编码相位均被拒绝，LINK staging 原始读回逐字相同。
  合法显式参数读回通过，随后恢复原 LINK 并确认停止。非查询命令无回复的 timeout
  记录仍保留，接受/拒绝由 SCPI 错误队列和实际 staging 读回共同判断。
- 限时自主路径：临时 trial 913031 / epoch 144 下观察到 origin persona 16，保持
  TX15/RX14，follower 沿用原 DATA 重定时。91.024 s 计时窗口内 NO1–NO4 的 good frame
  增量为 43007 / 34503 / 32894 / 30380，transport CRC 无增长；试验结束撤销许可。
  普通模式完整 profile 峰值为 1213.772 / 1002.252 / 1039.396 / 1022.132 us，
  自主模式为 894.100 / 1024.856 / 1096.108 / 1113.456 us，各板 SCHEDULE overrun
  均增长，完整 WCET 仍 FAIL。这是带 Core1 service 的有限观察，未验证 service
  blackout，也没有自主 prefix/skip/DMA 波形或特等席逐圈保全证据。
  `autonomous-analysis-r1.json` 绑定原始计时样本、phase 与物理故障计数；观察区间
  物理 fault 无增长。最终 `autonomous-rx-restore/` 已验证当前矩阵实际应用及四板
  STOP，RX/TX 回到基线共用行为；许可证撤销原始读回保留。
- 本切片仍 PARTIAL，`002F` 与长期目标保持 IN PROGRESS/active。有限普通模式
  零错误不证明最坏接收窗口、同一拒收帧的物理/私有副本绑定或自主 prefix/skip/DMA
  路径通过；新 payload 的实际 Flash commit/reboot 保全也尚需硬件证据。严格启动、
  flight-map 恢复、完整 Core1 WCET、正式 RAM、无 service 自主循环与特等席逐圈
  保全继续开放。当前 WCET 门限保留，registry/C11 状态不变。

### TDMA-PROGRESS-20260913-030 - origin 采样窗口、指令模型与跨日原始证据复核

- 日期：2026-09-13；TODO task ID：`TDMA-FLIGHT-002F`。以下数值为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260913/tdma-flight-origin-sample-eye/`，入口为
  `sample-eye-analysis-r1.json`、`review-r1.json` 与 `slice-manifest.json`。
- 基线提交 `6d3a3a7759154bd81e7ec99f6826678cdbeb6e2b`；四板保持 build
  `20260912144225`、源码指纹
  `b0d8daf294f26844c8cb01da84ff942101de8f39940bb30e88aa5a24caf345f0`。
  本轮为有界硬件诊断、离线分析与文档更新，无固件/PIO/正式工具实现变更、重新 OTA、
  新 build 或新 P3。六节点编译容量、PIO/DMA 分区及 NO5 排除范围保持。
- 采集启动于前一日，运行中的命令保留在
  `out/HardwareAcceptance/20260912/tdma-flight-origin-sample-eye/`；终止后复制至本根
  `started-20260912/`，`cross-day-archive-r1.json` 记录原路径、归档路径和逐文件 SHA。
  原件与内嵌路径不改写，118 个文件已逐一核对；新恢复、分析及检查使用本日目录。
- 使用前轮封存旧矩阵合法行进行 diagnostic replay。两组各完成两次 NO1 pad 采集，
  引脚组合为 RX_CLK/RX_CS/TX_DATA，即 `[28,27,24]`；每次 8192 words、8 ns 采样网格，
  经现有 SYNC_IO burst analyzer、StorageAO 和串口下载，采集耗时包含 SD 导出/下载。
  相位按四板 physical 原始读回核对，MARK/SCK 及其余 DATA 不变。

  | 组合 | origin / NO2 DATA phase | 前后快照主机窗口（s） | NO1 RX good / transport bad 增量 | NO2 / NO3 / NO4 transport bad 增量 |
  |---|---|---|---|---|
  | A | 15 / 15 | 650.588 | 158312 / 0 | 0 / 0 / 0 |
  | B | 15 / 14 | 661.730 | 157777 / 1075 | 0 / 0 / 0 |

- A 两次 capture 报告通过；B 两次报告均失败，其各自较短 capture/export 统计区间
  新增 CRC 为 138 / 88，不能相加替代包含下载时间的全窗口 1075。三次快照之间所查
  physical fault 无增长、相关接收计数无回退，快照时四板运行；稀疏快照不证明逐圈
  无瞬态事件。所有准备阶段的 startup、closed-loop、realtime 严格标志仍为 false。
- 失败完整保留：首轮 `r1` 在 NO4 ARM、恢复在 NO1 ARM 收到 result=5，无采集；
  `r2` 的 C（14 / 14）在 NO4 ARM 收到 result=5，未取得波形，包装器因断言失败退出。
  随后的恢复在 NO2 ARM 再次拒绝。该结果对应
  `DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED`，不是许可证拒绝；map 校验、
  snapshot/lock 竞争或 active 状态等具体原因未确定。旧包装器字段
  `fresh_matrix_restored_and_stopped=true` 实际仅验证 STOP；NO2 当时仍为 DATA14，
  本轮审计显式纠正其含义，不改写原始报告。
- 独立 `restore-baseline-r3/` 经完整 owner 生命周期应用当前矩阵 row1，各板 staging
  generation 为 `1789224796`，ARM 与运行参数均已读回。实际 soak `8.516 s`，四板
  RX good 增量 `2065/2056/2056/2057`，transport bad 均零。最终四板 STOP 通过，
  实际 MARK/SCK/DATA 分别为 `(11,11,15)/(9,10,15)/(10,10,15)/(11,10,15)`。
  startup timeout 仍使严格验收失败；该恢复不能补记 C 组合已完成。
- 当前 PIO 源经实际 pioasm 汇编后，普通 origin capture 的 WAIT1 SCK delay patch
  对应 `IN` 在 WAIT 成功后 14 / 15 个 clk_sys 拍，即名义 56 / 60 ns；这是指令相对
  时间，不是已测 pad 绝对采样时刻。固定包/clock 对齐的模型中，假设 clock 与 DATA
  各自附加零或一拍延迟：delay14 的四种组合均保留参考包，delay15 在仅 clock 多一拍
  时 A0/B0 分别改变 43 / 154 bytes。该差分延迟仅是反例假设，DMA/FIFO 可用亦为
  模型前提；不能宣称已测同步器偏移或确认硬件根因。普通程序重放自主波形也未验证
  自主 prefix、skip 与 DMA 路径。
- 固定参考相位 16 ns、各试验不重新对齐时，当前 A0 的离散同值区间为 -16..56 ns，
  A1/B0/B1 为 -24..56 ns（8 ns 网格）；B 的相邻 DATA 跳变有更多落在相对 64 ns 的
  采样格点。该区间不是模拟 setup/hold 保证，相对边沿量化误差仍在。实际 TX 指令模型
  同时确认 DATA15→14 会让首 bit 后的输出前移一拍，进一步说明共享参数不是纯 RX 调节。
- 四个选定参考帧的 sequence 为 `4512/83256/5076/85490`，每帧四个 mailbox 的
  magic/version、owner、target-mask 和独立 CRC16 均通过；transport 头部 CRC 与
  payload mailbox CRC 分开核验。选定物理参考帧不等同于固件拒收的同一帧，尚不能排除
  其他接收路径问题、保证后续全部 mailbox 或特等席逐圈保全。
- 下一 gate：独立表达 origin RX 采样时序与 TX 重定时，在 Calibration owner 的
  有效窗口、相对延迟反例和余量证据下提出修复，再由 TDMA owner 停止态配置并准入。
  保留同一坏帧波形/私有副本绑定与自主路径验证要求，不直接硬编码 phase。
  正确性闭合后继续 overlay/RX 成本削减；保留
  `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`、正式 RAM、特等席与节点容量后续项门禁。
- 文档检查、主控原始证据复核、本地文档提交及 SHA 封存见本根检查日志与
  `commit-proof.json`。切片 PARTIAL，`TDMA-FLIGHT-002F` 仍 IN PROGRESS，长期目标
  active；无 registry/C11 状态变更，无严格稳定性或产品验收结论。

