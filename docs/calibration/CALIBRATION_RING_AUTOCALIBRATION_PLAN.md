# 多板环路硬件自校准方案

Status: Draft
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_RING_AUTOCALIBRATION_PLAN.md`
Related: `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-25

本文档细化“板卡接收一条 SCPI 指令后，在硬件内完成板卡顺序搜索和 P1--P3 path-delay
训练”的目标架构。板卡顺序搜索定义为 P0，必须先于 CLK RTT 粗捕获、编码 marker 和
双向逐链路测距。PC 只负责发起、低频查询、保存和激活，不参与实时边沿、逐 trial 编排或
板卡身份判断。

本文档当前是实现计划。discovery wire、自动校准 SCPI 拼写、状态枚举、拒绝原因和质量
门限均为 candidate；在代码、单元测试和多板 HIL 完成前不构成冻结跨域契约，也不登记到
`DOCS_REGISTRY.md`。当前硬数字只引用代码符号或标注为 build/bench 诊断快照。

## 1. 目标与非目标

### 1.1 目标

1. 任意一块板收到一次自动校准 SCPI intent 后，板内协调器完成 P0--P3。
2. 板卡身份只使用 `*IDN?` 对应的唯一地址；COM、USB 枚举顺序和旧 NO 不参与身份判断。
3. P0 自动发现有向物理环序，接受后生成 NO、node map、topology CRC 和 generation。
4. P1--P3 复用现有 PIO/DMA persona 和计算原语，逐级形成可追溯 raw evidence。
5. 成功结果只写 staging；显式 `SAVE` 和 `ACTivate` 后才允许影响 VDC/DPLL。
6. 任一阶段失败都关闭训练时钟、恢复普通 TDMA persona，并保留旧 active calibration。
7. 节点容量使用代码事实源 `TDMA_RING_NODE_MAX`，不能在自动校准代码另建不同上限。

### 1.2 非目标

- 上电自动注入训练时钟。
- 通过高频 SCPI 轮询维持 RX window 或逐边沿驱动 PIO。
- 使用 COM 号、临时 NO 或 UI 排序作为 topology 事实。
- P0/P1/P2 结果直接写入 VDC/DPLL。
- 训练成功后自动覆盖 active calibration。
- 在本计划阶段冻结 discovery frame 的二进制布局或 SCPI 返回字段编号。

## 2. 当前实现基线与缺口

| 能力 | 当前基础 | 自动闭环缺口 |
|---|---|---|
| P0 topology | host 工具已有唯一 ID 枚举、邻接探测、环序判定和 NO 提交 | 缺板内 bootstrap discovery persona、重复一致性、topology snapshot 和分布式 commit |
| P1 CLK RTT | `SYSTem:TDMA:RING:TRAIN`、CLK burst/forward/capture 和四板 HIL 已有 | 完整多 master 搜索仍由 host 编排，未接入单指令协调器 |
| P2 coded | `CALibration:CLOCk:CODEd:*`、PIO/DMA raw capture 和 core1 有界相关已有 | 缺 reference 协调、板内重复统计以及 `PREPARE/ACK/commit` |
| P3 bidirectional | `CALibration:P3:*`、`t1..t4` 和逐链路 HIL 已有 | 缺 endpoint bias、topology freshness、整环 residual 和 active/staging gate |
| Calibration manager | 已有 core0 intent、core1 service 和 guarded 子功能 snapshot | 总状态仍是阶段壳，缺 `CalibrationAutoAO/FB/Vector` 和统一 transaction snapshot |
| 产品 SCPI | 对外文档已有 `CALibration:STARt/STOP/SAVE/ACTivate` 生命周期 | `ACTivate/ROLLback` 已连接到 CalibrationManager path snapshot 门禁；candidate 导入、SAVE/LOAD 和完整分布式 ACK 仍待完成 |

现有 P1/P2/P3 SCPI 保留为工程诊断入口。产品自动校准不得在这些 callback 上继续叠加同步
等待，而应新增一个统一 command mailbox 和板内非阻塞协调器。

## 3. HAOFV 所有权与执行边界

```text
USB CDC / USBTMC / UART SCPI
  -> core0: 参数解析、权限和系统状态检查
  -> CalibrationAutoCommandMailbox（guarded intent）
  -> CalibrationAutoAO：事务、阶段、统计、质量和 staging owner
  -> TdmaSchedulerAO / core1：persona、PIO、DMA、driver direction owner
  -> CalibrationAutoFB / core1 bounded worker：相关、边沿配对和固定上限统计
  -> CalibrationAutoVector / guarded snapshot
  -> core0 SCPI 只读查询
  -> Calibration staging package
  -> 显式 SAVE / ACTivate
```

### 3.1 core0 允许做的工作

- 校验 SCPI 参数、权限和 `SystemMode=IDLE/CAL`。
- 把单个 request 写入有界 command mailbox，并立即返回 accepted/epoch。
- 低频读取 guarded snapshot，格式化 SCPI 响应。
- 在训练完全停止且 core1 park/lockout 门禁通过后执行 flash/storage 操作。

### 3.2 core1 必须拥有的工作

- PIO instruction memory 和 persona 动态装载。
- SM、DMA、IRQ、TX/RX FIFO 和 ISO1452 固定方向控制。
- discovery frame 固定字段飞行写入、CLK 转发、marker TX/RX 和四边沿捕获。
- 有界 raw-sample correlation、固定大小 histogram 和 trial completion。
- 在 deadline 内恢复普通 persona；不得等待 USB、日志或 flash。

### 3.3 Calibration 域拥有的工作

- topology、NO/node map、path-delay、residence、endpoint bias 的解释和接受门禁。
- epoch、generation、CRC、freshness、ACK/NACK bitmap 和 staging/active 生命周期。
- 对 raw TDMA evidence 做质量判断；TDMA 域不得生成 active calibration。

RefMem/flight frame 只承载 command、ACK/NACK 和固定摘要。raw capture、长 histogram 和完整
历史不进入 RefMem；它们保留在有界 RAM buffer，必要时由 Storage 域导出。

## 4. 总体事务与状态机

```text
IDLE
  -> PRECHECK
  -> P0_DISCOVERY_PREPARE
  -> P0_DISCOVERY_RUN
  -> P0_TOPOLOGY_VERIFY
  -> P0_TOPOLOGY_COMMIT
  -> TRAIN_PREPARE
  -> WAIT_NODE_ACK
  -> P1_CLOCK_COARSE
  -> P2_CLOCK_CODED
  -> P3_ENDPOINT_BIAS
  -> P3_LINK_RANGING
  -> QUALITY_GATE
  -> RESTORE_NORMAL_PERSONA
  -> STAGING_READY
  -> DONE
```

任意运行态都允许异步进入 `ABORTING -> RESTORE_NORMAL_PERSONA -> ABORTED`。任意错误进入
`FAILING -> RESTORE_NORMAL_PERSONA -> FAILED`。只有 `STAGING_READY` 可以生成新的 staging
CRC；FAILED/ABORTED 不覆盖之前的 staging 或 active。

状态机不得使用长阻塞延时。每个状态只提交一次有界动作，后续由 service tick 检查
completion/deadline。每次状态转换递增 transaction sequence，并发布统一 snapshot。

## 5. P0：板卡顺序搜索

### 5.1 为什么必须独立于旧 topology

自动校准启动时可能没有 NO、NO 已过期、板卡从环中间插入，或供电后 USB 枚举顺序改变。
因此 P0 只能依赖：

- 本机 immutable unique ID；
- 固定的 CLK/DATA/SYNC 物理方向；
- `TDMA_RING_NODE_MAX` 容量；
- 一个不依赖 node map 的 bootstrap discovery persona。

旧 NO 只可作为 UI 显示缓存，不能作为发现输入。若 P0 失败，旧 NO 不得被改写。

### 5.2 reference/anchor 选择

按以下优先级确定 anchor：

1. 维护人员显式配置的 unique ID，且该 ID 在本轮发现结果中存在；
2. 当前 active topology 仍 fresh 时沿用其 NO.1 unique ID；
3. 没有 fresh topology 时，以接收 SCPI 的本板 unique ID 作为临时 anchor。

接受后的 NO.1 固定为 anchor，沿下游物理方向依次生成后续 NO。NO 只是 accepted topology
的显示编号，校准表主键始终是 unique ID link pair。

### 5.3 bootstrap discovery frame

候选逻辑字段如下，暂不冻结 bit layout：

```text
DiscoveryHeader
  version
  epoch / nonce
  anchor_uid
  operation = DISCOVER | VERIFY | COMMIT | ABORT
  next_node
  hop_limit
  topology_candidate_crc

DiscoveryNodeEntry[TDMA_RING_NODE_MAX]
  node_uid
  predecessor_uid
  local_rx_quality
  claim_flags
  node_map_crc

DiscoveryTrailer
  ack_bitmap
  nack_bitmap
  frame_crc
```

使用固定数组而不是变长 append，避免 core1 动态分配。最终编码长度、CRC 和字段偏移必须由
命名符号生成，并在 C/Python golden vector 和 HIL 同时形成后再冻结。

### 5.4 飞行发现算法

1. anchor 在首个 node entry 写入自己的 UID，生成新的 epoch/nonce，并发送 `DISCOVER`。
2. 每个节点第一次看到该 epoch 时，只检查固定 header 和已占用 node entry：
   - 若本 UID 未出现，则写入 `node_entry[next_node]`；
   - `predecessor_uid` 取上一个已占用 node entry 的 UID；
   - 记录本地 RX quality，更新 node-map CRC 和 `next_node`；
   - 立即向下游转发，不等待 core0。
3. 已经 claim 的节点再次看到同 epoch 时透明转发，不重复占 node entry。
4. anchor 收到相同 nonce 的返回 frame 后停止该 frame，禁止再次进入环路。
5. 未在 deadline 内返回视为开链；node entry 溢出、重复 UID、hop limit、CRC 或方向错误均拒绝。

discovery 飞行写入只处理固定 offset，不实现通用 SCPI 或通用 frame parser。其资源由 TDMA
foundation profile 声明，Calibration 只解释返回 node entry 和质量。

### 5.5 邻接验证

单次 token 顺序只能作为 candidate。随后在 bootstrap persona 中执行 `VERIFY`：

1. 根据 candidate order 生成相邻 link 列表。
2. 每个 verification window 只授权一个 source driver 和其 candidate successor receiver。
3. 记录 TX/RX frame、word、edge、bad-header、CRC、timeout 和非目标节点误触发计数。
4. 对全部 link 完成一轮后形成 directed adjacency matrix。
5. 按 active quality profile 重复 discovery/verify；每轮 UID order、predecessor/successor 和
   adjacency 必须一致。

接受条件：active node set 非空且不超过 `TDMA_RING_NODE_MAX`；每节点入度、出度均为一；
从 anchor 出发恰好遍历全部节点并回到 anchor；没有自环、分叉、多个闭环或非目标接收。

### 5.6 P0 输出与提交

P0 生成 `CalibrationTopologySnapshot`：

```text
epoch
anchor_uid
node_count
ordered_uid[]
predecessor_uid[] / successor_uid[]
directed_adjacency
node_map
topology_crc32
topology_generation
quality_summary
accepted / reject_reason
```

只有 accepted snapshot 才进入 `P0_TOPOLOGY_COMMIT`。全节点收到相同 topology CRC 后返回
ACK；anchor 收齐 active-node ACK bitmap 才更新 staging NO/node map。持久化留给显式
`CALibration:SAVE`，P0 不直接擦写 flash。

## 6. 分布式 PREPARE/ACK/COMMIT

普通 TDMA persona 仍可传输控制帧时，reference 为下一批测量发送：

```text
TRAIN_PREPARE
  epoch
  stage
  topology_generation / topology_crc
  profile_crc / schedule_crc
  persona_id
  master_uid / source_uid / destination_uid
  frequency_profile
  trial plan
  commit_seq
```

各节点只做资源和 generation 检查，将 persona/参数写入本地 staging，并返回 ACK/NACK。
reference 收齐 active-node ACK 后发送 COMMIT；所有节点在同一 `commit_seq` 切换 persona。

训练 persona 可能无法承载普通 DATA frame，因此每个有界 batch 完成后必须先恢复普通 persona，
再传输 evidence 和准备下一 batch。第一版不预装整个 P0--P3 大计划，避免一次 command miss
导致长时间失联。后续只有在 RAM、恢复和故障注入证据充分时才考虑合并 batch。

以下任一情况取消本 epoch：ACK 缺失、NACK、topology/profile CRC 改变、commit miss、节点
重启、persona generation 不一致。取消后所有已 ACK 节点执行 ABORT 并恢复普通 persona。

## 7. P1：CLK RTT 粗捕获

### 7.1 目的

P1 只获取每个 master 的整环 CLK RTT acquisition bracket，并证明 CLK 能逐节点转发返回。
它包含线缆、收发器和 follower forwarding residence，不能直接生成单 link delay。

### 7.2 板内流程

1. 按 P0 accepted order 轮流选择每个 UID 为 master。
2. PREPARE follower `CLK_FORWARD` 和 master `CLK_BURST_CAPTURE`。
3. 从 active quality profile 的 pulse-count 起点执行指数搜索。
4. 找到 `ALL_NON_OVERLAP -> MIXED -> ALL_OVERLAP` 过渡后，在 bracket 内缩小范围。
5. 每个 decision point 按 profile 重复，分别记录 overlap/non-overlap/mixed/rejected。
6. 每个 master 完成后恢复普通 persona，再准备下一个 master。

### 7.3 P1 输出

- master unique ID、epoch、profile 和 persona generation；
- `N_low/N_high` 及 duration bracket；
- mixed region、timeout、missing/duplicate pulse；
- DMA/PIO/IRQ counters；
- CLK frequency、high/low、duty 和返回质量；
- `DIAGNOSTIC_ONLY` 标志。

P1 接受只允许进入 P2，不能进入 calibration staging delay 表。

## 8. P2：编码 marker 与相关测距

### 8.1 目的

P2 使用 P1 bracket 限制 lag 搜索范围，在 raw Manchester waveform 上直接相关，识别返回
marker 的 chip 和 sample 相位。硬件分辨率继续由 `BOARD_SYS_CLOCK_HZ`、PIO 程序和
snapshot 中的 `sample_period_ns` 声明；统计均值不得伪装成更高 hardware resolution。

### 8.2 板内流程

1. reference 根据 P1 bracket 选择 active codebook/robust marker profile。
2. PREPARE master coded TX + oversample RX，followers 只透明转发 CLK。
3. PIO/DMA 完成固定大小 TX/RX；core1 在有界 lag 集合执行 XOR/popcount correlation。
4. 检查 header/inverse/CRC、极性、主峰、第二峰、distance、margin 和完整 capture。
5. 按 profile 重复并形成 lag histogram、reject histogram 和跨 master 一致性。
6. 任一 raw buffer 完成发布前保持 DMA 独占；SCPI/core0 不得读取正在写入的 buffer。

### 8.3 P2 输出

- 每个 master 的 coded RTT、best/second lag、distance 和 margin；
- detected polarity、marker flags、capture origin 和 DMA counts；
- coarse bracket 与 coded result 的一致性；
- rejected reason 和统计质量；
- 下一阶段 P3 的 bounded capture window。

P2 仍测量整环 marker RTT。它提高定位可靠性，但不替代 P3 的逐链路双向测距。

## 9. P3：endpoint bias 与逐链路双向测距

### 9.1 P3A endpoint bias

每块板先在与板间 P3 相同的 PIO persona、采样路径和 driver direction 下执行 reference
loopback，形成：

- board unique ID；
- persona/profile/topology generation；
- local `t1..t4`；
- endpoint bias candidate；
- bias generation、质量、温度/电源诊断摘要；
- `CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID`。

reference loopback 结果本身始终是 reference evidence，不作为 link delay。bias generation
必须与后续 P3 link sample 匹配；persona、profile 或 topology 变化使其 stale。

### 9.2 P3B per-link ranging

按 P0 order 对每条 `A -> B` 相邻 link 执行：

```text
t1 = A.forward_line_TX
t2 = B.forward_line_RX
t3 = B.return_line_TX
t4 = A.return_line_RX

residence_B = t3 - t2
raw_path_sum_AB = (t4 - t1) - residence_B
corrected_path_sum_AB = raw_path_sum_AB - endpoint_bias_AB
delay_AB = corrected_path_sum_AB / 2
```

`forward_line`、`return_line` 和可选的 `sync_line` 是本轮 persona 的角色，
不是固定的物理线名称。当前网线对称链路的两个 P3 配置为：

```text
CLK_DATA: forward=CLK, return=DATA, sync=CS
CS_DATA:  forward=CS,  return=DATA, sync=CLK
```

同步线只用于打开捕获窗口和关联 epoch，不进入 `t1..t4`、residence 或 path-sum。
两组配置必须分别动态装载和卸载 PIO persona；`BOTH` 只是顺序执行两次独立试验。

initiator/responder 在同一 epoch 下采集各自边沿。host 不拼接不同 epoch，不补造缺失边沿。
Calibration gate 检查 hardware latch、SYNC match、DMA complete、edge order、bias generation、
topology freshness、clock-rate bound 和非负 path-sum。

### 9.3 P3C cumulative/residual

逐 link 完成后计算：

```text
sum(corrected_path_sum_per_link)
whole_ring_observed_rtt
forwarding_residence_sum
endpoint_bias_sum
residual
```

不同 persona 或不同边沿定义的历史 RTT 不直接相减。只有同 epoch family、同 topology、同
profile 和可解释 edge definition 的 evidence 才进入 residual gate。residual 超门限时只发布
诊断结果，不平均摊回各 link。

### 9.4 频率策略

自动校准引用 `tools/calibration_ring_validate/calibration_link_frequency_policy.py` 的当前验证
策略作为迁移输入：

- `REQUIRED_FREQUENCY_LADDER_MHZ` 定义每次必须执行的完整阶梯；
- `LIMITED_RX_FREQUENCY_MHZ` 每次都必须实际执行，低频失败也不得跳过；
- limited RX 任一 reject 发布 `FALLBACK_25MHZ`，目标回退档由
  `LIMITED_RX_FALLBACK_MHZ` 声明；
- limited RX 不能单独提高 stable maximum，也不放宽 frequency/duty/DATA width gate；
- stable-required 档失败时仍完成 limited RX 诊断，但本轮 staging 不具备 active eligibility。

固件实现时应把该策略映射到 TDMA operating profile 的命名 level，并形成单一代码事实源；
不能长期让 Python 和固件各自维护一份可漂移的频率表。

## 10. 统一质量门禁

### 10.1 topology gate

- node set、order、adjacency 和 anchor 多轮一致；
- topology CRC/generation 在 P0--P3 全事务内保持不变；
- ACK bitmap 覆盖全部 active nodes；
- unique ID、NO 和 node map 一一对应。

### 10.2 transport gate

- persona/profile/schedule generation 匹配；
- DMA complete 且没有 overrun；
- PIO 无 stall，FIFO/IRQ completion 完整；
- CLK frequency/duty 和 DATA pulse width 通过；
- STOP/restore 后普通 persona counters 能继续推进。

### 10.3 measurement gate

- P1 bracket 有效且 mixed/reject 比例满足 profile；
- P2 marker/header/CRC/correlation/margin 通过；
- P3 四边沿完整、顺序正确，bias valid，topology fresh；
- 重复统计、跨方向 asymmetry 和 cumulative residual 通过；
- `DIAGNOSTIC_ONLY` 未清除时禁止 active eligibility。

### 10.4 lifecycle gate

- 失败不覆盖旧 staging/active；
- staging package 具有 calibration/topology/profile/bias generation 和 CRC；
- `SAVE` 经过 storage 与 flash lockout；
- `ACTivate` 只允许 IDLE，并使旧 `SYNC:CHECk` 结论失效；
- VDC/DPLL 只消费 active、fresh、CRC 正确且非 diagnostic-only 的结果。

## 11. SCPI 候选接口

为避免破坏现有“指定单段快速校准”的 `CALibration:STARt`，整环自动流程建议使用独立
`CALibration:RING` 子树。以下拼写是 candidate，代码和接口文档同步实现后再冻结。

| 指令 | 参数 | 即时响应 | 语义 |
|---|---|---|---|
| `CONFigure:CALibration:RING:ANCHor` | `AUTO` 或 unique ID | accepted | 配置 anchor 选择策略，只写 staging config |
| `CONFigure:CALibration:RING:PROFile` | profile name | accepted | 选择命名质量/重复/timeout profile，不开放任意实时参数 |
| `CALibration:RING:STARt` | `FULL` / `TOPOLOGY` / `PATH` | `accepted,epoch` | 发布一次异步 transaction intent |
| `CALibration:RING:STOP` | 无 | accepted | 请求安全中止和 persona 恢复 |
| `READ:CALibration:RING:STATe?` | 无 | 固定 state block | 查询阶段、进度、ACK/NACK 和错误 |
| `READ:CALibration:RING:RESult?` | 无 | 固定 summary block | 查询 topology、质量、stable/limited 状态和 staging 摘要 |
| `READ:CALibration:RING:NODE?` | node index | node block | 按 accepted node 读取 unique ID 和节点质量 |
| `READ:CALibration:RING:LINK?` | link index | link block | 按相邻 link 读取 delay/residence/bias/质量 |

`STARt` callback 不等待 P0/P1/P2/P3 完成。USB CDC、USBTMC 和 UART 使用同一 SCPI handler；
接口差异不能改变事务语义。查询只读取 snapshot，不锁住 core1。

建议 state block 固定字段：

```text
epoch,state,stage,anchor_uid,node_count,current_master,current_link,
frequency_profile,repeat_index,ack_bitmap,nack_bitmap,progress,
last_error,topology_generation,calibration_generation,staging_crc
```

建议 result summary 固定字段：

```text
epoch,result,topology_crc,profile_crc,node_count,link_count,
stable_profile_status,limited_rx_status,bias_generation,
accepted_link_bitmap,rejected_link_bitmap,quality_flags,
reject_reason,staging_generation,staging_crc
```

node/link 明细使用分页或 index query，避免单条 SCPI 响应随节点数无界增长。

### 11.1 典型操作

```scpi
*IDN?
CONFigure:CALibration:RING:ANCHor AUTO
CONFigure:CALibration:RING:PROFile FIELD
CALibration:RING:STARt FULL
READ:CALibration:RING:STATe?
READ:CALibration:RING:RESult?
READ:CALibration:RING:NODE? 0
READ:CALibration:RING:LINK? 0
CALibration:SAVE <cal_id>,NODE
CALibration:ACTivate <cal_id>
```

上位机允许低频轮询或等待 SRQ/operation event，但不能用查询频率推进状态机。串口断开后
板内事务继续执行；重新连接后通过 epoch 查询结果。

## 12. 数据模型候选

### 12.1 CalibrationAutoRequest

```text
guard / command_seq
operation
profile_id
anchor_policy / anchor_uid
requested_scope
request_epoch
expected_active_generation
```

### 12.2 CalibrationAutoSnapshot

```text
guard / snapshot_seq
transaction state / stage / substate
current master/link/profile/repeat
deadline and progress
ack/nack bitmaps
topology/profile/persona/bias/calibration generations
quality/reject summary
restore status
staging CRC
```

### 12.3 CalibrationLinkResult

```text
source_uid / destination_uid
source_node / destination_node
topology/profile/bias generation
accepted frequency profile
t1..t4 evidence reference
residence
raw/corrected path-sum
delay estimate
repeat statistics / jitter / uncertainty
quality flags / reject reason
```

结果数组容量从 `TDMA_RING_NODE_MAX` 派生。raw capture buffer 保持现有静态上限和 DMA owner，
不得按节点数动态分配大块内存。

## 13. 失败、恢复与看门狗

| 故障 | 动作 | 结果保护 |
|---|---|---|
| open ring / topology 不唯一 | 终止 P0，关闭 discovery driver | 不更新 NO/topology staging |
| ACK timeout / NACK / commit miss | 广播 ABORT，恢复已准备节点 | 当前 epoch FAILED，旧 staging/active 保留 |
| DMA overrun / PIO stall / missing edge | 停止本 trial，收割 counters，恢复 persona | 记录具体 stage/link/profile reject |
| frequency/duty/pulse width 失败 | 按 profile 继续必要诊断或降级 | 不放宽门限，不隐藏 limited RX reject |
| topology/profile generation 变化 | 立即标 stale 并终止 | 已采 P1--P3 evidence 不进入 staging |
| SCPI/USB 断开 | 板内继续，结果按 epoch 保留 | 不依赖 host keepalive |
| 用户 STOP | 进入 ABORTING，执行统一恢复 | 不生成新 staging |
| watchdog/reset | boot 识别未完成 transaction，保持训练 driver 关闭 | active calibration 不变，发布 reset reason |
| restore 失败 | 强制关闭 driver、释放 DMA/SM，进入 FAULT | 禁止普通 TDMA START，等待维护复位 |

训练状态必须周期性喂入健康监控，但不能为了避免 watchdog 而隐藏无进展状态。每个 stage
记录 deadline、last progress tick 和 completion counter；超时统一走恢复路径并输出 log。

## 14. staging、持久化与激活

自动校准成功后生成 staging package：

```text
calibration ID / generation / CRC
build ID
topology generation / CRC / ordered unique IDs
profile/schedule/persona generation and CRC
bias generation
per-link results and quality
stable/limited RX status
timestamp / temperature / supply diagnostic summary
raw evidence index
```

生命周期固定为：

```text
AUTO CAL DONE
  -> STAGING_READY
  -> optional CALibration:SAVE
  -> explicit CALibration:ACTivate
  -> invalidate old SYNC check
  -> SYNC:CHECk / DPLL relock
```

P0 接受后可更新 RAM 中的 NO/node staging，但只有 SAVE 才持久化。flash erase/program 必须由
core0 storage owner 执行，并先获得 core1 park/lockout ACK。校准热路径不访问 XIP 写操作。

## 15. 实施待办

### A0：统一 transaction 基础件

- [ ] 定义 candidate `CalibrationAutoRequest/Snapshot/LinkResult` 和 reject category。
- [ ] 建立 core0 command mailbox、core1 completion mailbox 和 seqlock/guarded snapshot 单测。
- [ ] 增加 `CalibrationAutoAO/FB/Vector`，停止在 `calibration_manager_service()` 每轮清零状态。
- [ ] 接入系统 IDLE/CAL、Trigger RUN、TDMA stopped 和资源 owner 门禁。
- [ ] 实现统一 ABORT/restore，不依赖具体 PIO persona 的成功路径。

### A1：P0 板内 topology discovery

- [ ] 定义 bootstrap discovery persona 的 PIO/SM/DMA/profile resource claim。
- [ ] 实现固定 discovery node-entry 飞行 claim、anchor return stop 和 hop/deadline gate。
- [ ] 实现 directed adjacency VERIFY、重复一致性和 topology quality。
- [ ] 发布 `CalibrationTopologySnapshot`，按 unique ID 生成 NO/node staging。
- [ ] 完成从最小双板到 `TDMA_RING_NODE_MAX` 的模拟、主机单测和 HIL。

### A2：分布式 PREPARE/ACK/COMMIT

- [ ] 定义普通 TDMA control payload 的 candidate 字段和 golden vector。
- [ ] 实现 active-node ACK/NACK bitmap、commit sequence 和 generation 检查。
- [ ] 实现节点重启、ACK 丢失、commit miss 和 topology change 故障注入。
- [ ] 每个 batch 完成后验证普通 persona 和 TDMA counters 恢复。

### A3：P1 自动化

- [ ] 把现有 CLK TRAIN 单 trial 封装为非阻塞 P1 step。
- [ ] 实现按 accepted UID order 的 master 轮换、指数搜索、缩窗和重复统计。
- [ ] 将 bracket/mixed/reject 只发布给 P2，不写 path-delay staging。
- [ ] 复核每个 frequency profile 的 CLK frequency/duty 和 driver restore。

### A4：P2 自动化

- [ ] 将 P1 bracket 直接绑定 coded request，禁止 host 注入实时 lag window。
- [ ] 完成板内 repeat/histogram 和跨 master quality gate。
- [ ] 固化 raw buffer generation、DMA ownership 和 correlation deadline。
- [ ] 验证错位、反相、缺失/重复 chip、低 margin、截断和残留 epoch。

### A5：P3 bias 与逐 link 自动化

- [ ] 在同一 P3 persona 下完成每板 reference loopback 和 bias generation。
- [ ] 按 P0 order 自动执行全部 link 的 initiator/responder `t1..t4`。
- [ ] 强制完整频率阶梯和 limited RX/fallback 策略。
- [ ] 完成四边沿、clock/data timing、DMA/stall、asymmetry 和 freshness gate。
- [ ] 实现 per-link cumulative 与同 persona whole-ring residual。

### A6：质量、staging 和 VDC gate

- [ ] 形成 topology/bias/profile/calibration generation chain 和 staging CRC。
- [ ] 只有 bias valid、topology fresh、非 diagnostic-only 才设置 active eligible。
- [ ] 实现 SAVE/LOAD/ACTivate/ROLLback 和分布式 ACK。
- [ ] ACTivate 后使旧 SYNC check 失效，验证 VDC/DPLL 不消费 stale calibration。

### A7：SCPI 与工具

- [ ] 实现 `CALibration:RING:*` candidate command，并保持 callback 短事务。
- `[~]` 修复当前 `CALibration:ACTivate/ROLLback` stub 与产品接口语义偏差；candidate 导入、
  `STARt/STOP/SAVE/LOAD` 的完整事务和分布式 ACK 仍待完成。
- [ ] 固化 state/result/node/link 固定字段及解析测试。
- [ ] 新增 host 工具只做 `STARt -> 低频查询 -> 导出 evidence -> SAVE/ACTivate`。
- [ ] 所有工具先用 `*IDN?` 校验本地端点，不使用 COM 号作为板卡身份。

## 16. 验证矩阵

| 范围 | 必测场景 |
|---|---|
| P0 topology | 双板到最大节点数、从环中间插板、anchor 变化、旧 NO、开链、反接、分叉、多个闭环、重复/缺失 UID、非目标误触发 |
| coordination | ACK 丢失/NACK/迟到、commit miss、节点重启、CRC/generation 改变、重复 command、SCPI 断开 |
| P1 | non-overlap/overlap/mixed、缺 pulse、重复 pulse、timeout、四主/多主轮换 |
| P2 | 正常/反相、chip 缺失/重复、低 margin、错误 header/CRC、capture truncation、DMA overrun、PIO stall |
| P3 | edge missing/order、epoch mismatch、bias invalid/stale、topology stale、direction asymmetry、negative path、residual fail |
| frequency | 每次执行 `REQUIRED_FREQUENCY_LADDER_MHZ`；limited RX 成功/失败和 fallback；CLK/DATA frequency/duty/width 全门禁 |
| recovery | 每个状态执行 STOP、USB 断开、watchdog/reset、restore failure、重新进入普通 TDMA |
| lifecycle | staging 不覆盖 active、掉电 SAVE、CRC 错 LOAD、分布式 ACTivate/ROLLback ACK、SYNC check 失效和 DPLL 拒绝 stale |

HIL 报告必须绑定 build、ordered unique IDs、topology/profile/bias generation、线缆、收发器、
供电入口、温度、频率策略和 evidence directory。COM 号只允许作为当次 transport 备注。

## 17. 完成标准

1. 一条 `CALibration:RING:STARt FULL` 可在无有效 NO 的情况下完成 P0--P3。
2. P0 对 active node set 形成唯一闭环、稳定顺序和 topology generation；错误拓扑明确拒绝。
3. P1/P2/P3 全部实时动作驻留板内，断开 host 后事务仍能结束并安全恢复。
4. 每条 link 都有同 epoch 的 bias、`t1..t4`、residence、path-sum、delay 和质量证据。
5. 每次验证完整执行 shared frequency ladder，limited RX 不污染 stable acceptance。
6. 任一失败不改旧 staging/active，不留下持续 CLK、占用 DMA/SM 或半切换 persona。
7. staging package 的 CRC/generation/freshness 完整，VDC/DPLL 只接受显式激活后的有效结果。
8. 双板、当前多板和 `TDMA_RING_NODE_MAX` 容量边界均有自动化测试与可追溯 HIL evidence。
