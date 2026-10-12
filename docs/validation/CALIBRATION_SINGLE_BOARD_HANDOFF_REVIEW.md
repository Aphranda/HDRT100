# 单板双槽位校准：有限提交与换机交接

Status: Draft
Domain: Calibration
Canonical: `docs/validation/CALIBRATION_SINGLE_BOARD_HANDOFF_REVIEW.md`
Related: `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`, `config/hardware_acceptance/p3_bench_single.json`
Last updated: 2026-09-22

## 当前优先事项

用户追加要求：后续 T0-T3（TRN-00 MARK、TRN-01 SCK、TRN-02 DATA、TRN-03 闭环）
统一按单板真实双槽位构建，发送与响应并行，使用同一硬件采集时基。不是放宽多板工具
的板数限制，不复制物理快照冒充第二端点。四板流程与证据范围保留独立；每个阶段先完成
软件验证、构建、OTA 和同版本 TDMA 恢复实测，再进入下一阶段。

2026-09-21 用户改为继续 P3。此前有限提交/推送被中断，**尚未创建提交或推送，换机包也未生成**；
下文打包、提交及恢复步骤仅为待执行交接计划，不是已完成事实。已有宿主改动暂存保持原样。
最新实测 `out/tdma-budget/p3-resume-20260921-r1/` 中粗校准、coded 和单板夹具 Latency Cal
已通过；该次 MARK/residence/SCK/DATA 在子工具入口拒绝单板，诊断继续不代表通过，最终停在
TRN-02 MARK 参数交接检查。随后基础 MARK 已支持单板 origin/return 并完成正测、超时负测与
SD 原始采集。随后真实并行 MARK 双槽位已完成四边沿及完整码字采集切片；
accepted MARK/residence 矩阵、DATA 和 TRN-03 仍未闭合。SCK 已完成独立发射偏移诊断、
状态查询超时修复，以及实际接收相位扫描和重新装载后的残差切片；这不等于生产训练矩阵。
独立 MARK RXPHASE 也已构建、OTA 并通过扫描/重载残差；DATA 同发次并行采集已构建、
OTA 和板测，扫描原点修复后的主码本全链及重载零残差通过；码本零/二仍因分组候选不一致
被拒，不能将单轮通过称为全码本稳定。后续实际 PIO 矩阵装载和正式反馈仍待闭合。
先关闭风险、继续板测，不提交。

## 提交边界和用户要求

本记录是换机 checkpoint，不是冻结契约、产品放行或完整 P3 验收凭证。
用户批准本次带 risk 的有限提交和推送，并明确：**本次推送后，下一步必须先解除以下
OPEN 风险，补齐证据和独立复核，才能再次提交；不得滚动进行新的带 risk 提交。**
本次代码和文档分离提交后一起推送，属于同一次有限交付。此后可在工作区修复、构建、
OTA 和测试，但不得在风险未关闭时创建后续提交。不得通过关闭 hook、伪造凭证、放宽
设备源码白名单或把单板数据称为多板证据来规避门禁。

提交范围仅为粗校准、coded、Latency Cal 宿主工具、对应 Python 测试、P3 验收编排和
单板夹具配置。设备侧固件、先前 TDMA/VDC 预算与 GUI 改动继续保留为未提交工作区，
由换机包转移；本次不提交设备源码、不生成成功 receipt、不宣称新主机仅 git pull 即可重建板端版本。

## 当前硬件与证据（本次快照，非事实源）

- 分支：`feature/node-sequence-reservation-trigger`；本次工作起点：`0c913e83f241fa8b40c059792141ae6051a85139`。
- 板卡 UID：`839E1AE79EA20F31`；最后端口 CDC `COM3`；换机必须按 UID 重新发现端口。
- 当前已 OTA 固件 build：`20260921092708`，路径 `out/build/data-loop-20260921-r1/`。
- OTA：`out/tdma-budget/data-loop-20260921-ota-r1/summary.json`，通过。
- OTA 包 SHA-256：`1B55B8FFF90BD5F05E41AC6571E6D4FE94B359D41520722F4508F960A03F62CB`。
- 构建成功日志为 `out/tdma-budget/data-loop-20260921-build-r2.log`；后续未构建改动不等于已在板端运行。
- 用户确认单板网线为 10 cm，四板网线为 5 m；两套夹具不能共用延迟窗口。
- 单板逻辑槽位为 TX 和外部回环 RX，同一物理 UID、同一时钟；不是两块独立板。
- 最后设备 ring STOP、probe 关闭、CDC；Core1 预算仍为 1.5 ms，OPMODE level 7 的帧周期为 1 ms，二者不能混淆。
- 测试未执行参数 SAVE Flash；普通 APPLY 和配置保持 RAM 临时语义。
- 尝试恢复 `TOPology 1,0,0` 被设备拒绝并已保留错误记录；最终维持停止状态及逻辑双槽位，勿误报已恢复单槽。

| 实测 | 结果 | 原始证据（本地保留，换机包待生成） |
|---|---|---|
| 粗 CLK，单板双槽位 | 通过；重复采样得到 N_low=0、N_high=1；仅诊断 overlap bracket | `out/tdma-budget/coarse-dual-slot-r2/summary.json` |
| coded，单板双槽位 | 3/3 通过；lag=19、distance=0、margin=487..489；sequence 新鲜、marker flags 完整 | `out/tdma-budget/coded-dual-slot-r1/summary.json` |
| Latency Cal，两组信号、频率阶梯 | 18/18 通过；端点包含的未修正 delay 为 52 ns；30 MHz 仍属 limited/diagnostic | `out/tdma-budget/coarse-coded-p3-latency-r1/summary.json` |
| P3→TDMA→重复 P3 STOP→TDMA | 收发通过；重复 STOP 前后 PHYS 快照相同，无程序泄漏复发 | `out/tdma-budget/coarse-coded-idle-stop-owner-r1.txt`、`coarse-coded-after-stop-tdma-r1/summary.json` |
| 最终 TDMA 短帧 | 数据闭环通过、bad/overrun 增量为零；`formal_feedback_evidence=false`，不得标注正式时间戳闭环通过 | `out/tdma-budget/coarse-coded-final-tdma-r1/summary.json` |
| 完整单板验收编排 | 粗校准/coded 通过，旧四板夹具范围拒绝 52 ns；无通过凭证 | `out/tdma-budget/coarse-coded-acceptance-r1/` |
| 新夹具单板编排 | 粗校准/coded/Latency 通过，后续训练入口拒绝；诊断继续后停在 DATA 参数交接；无成功 receipt | `out/tdma-budget/p3-resume-20260921-r1/` |
| 基础 MARK origin/return | 实物单记录 CRC/correlation 通过，不代表 follower residence 或 latency 通过 | `out/tdma-budget/p3-marker-single-20260921-r1/summary.json` |
| MARK idle-high 负测 | 设备按预期 timeout 拒绝；初次宿主 board_count 误报已修复并复跑通过 | `out/tdma-budget/p3-marker-single-20260921-idle-high-r2/summary.json`；首次失败保留在 `p3-marker-single-20260921-idle-high/` |
| MARK SD 原始采集与离线复核 | 保存、下载及相关性复核通过；重建前缀的 replay，不是独立时间测量 | `out/tdma-budget/p3-marker-single-20260921-raw/marker_capture.json`、`correlation.json`、`correlation.svg` |
| MARK 后 TDMA 恢复 | 电气数据闭环通过；`formal_feedback_evidence=false`，正式时间戳风险仍 OPEN | `out/tdma-budget/p3-marker-single-20260921-after-tdma/summary.json` |
| MARK 身份绑定修复后复测 | 正测与 idle-high 负测通过；准备/完成记录及请求参数一致 | `out/tdma-budget/p3-marker-single-20260921-bound-r1/summary.json`、`p3-marker-single-20260921-bound-fault-r1/summary.json` |
| 身份绑定修复后 TDMA | 电气数据闭环通过，无 bad/overrun 增量；正式反馈仍 false | `out/tdma-budget/p3-marker-single-20260921-bound-after-tdma/summary.json` |

单板配置现用 `minimum_link_delay_ns` / `maximum_link_delay_ns`，夹具解释在
`p3_fixture`：本次暂定 44..60 ns，来源为包含端点延迟的 52 ns 实测基线，加减两倍
4 ns 采样间隔。该范围是诊断暂定基线，**不是 10 cm 网线传播延迟或独立计量标定**。
四板 `p3_bench.json` 的 70..90 ns 保持不变。配置修改后的 HIL 已尝试，夹具门禁通过，后续训练未通过。

### 旧 MARK 证据边界（历史诊断快照，非事实源）

旧版离线相关性结果为 lag 1、distance 1、margin 480，重建前缀为 19 个采样，
marker 起点映射到原始采集的 -18 个采样。因此不能把输出中的 8 ns loop RTT 当作
真实传播延迟，也不能拿它替换 Latency Cal 的独立四边沿结果。宿主输出明确标记
`SINGLE_BOARD_MARKER_CORRELATION_DIAGNOSTIC`、`latency_certified=false` 和
`follower_residence_measured=false`，不生成单板生产 phase contract。

源码核对发现 `tdma_pio_spi_phys_marker_restored.inc` 使用的 capture 始终等待 RX CS，
而 origin decoder 注释和 tick 计算仍假设本地 TX 与两 NOP 起点。下一固件切片需修复
角色化门控及实际 WAIT/phase 到采样的关系；不能直接把 initializer 的 RX 参数换成 TX，
因为同一参数还用于 GPIO 输入方向设置，会导致输出脚被改为输入。该固件修复尚未实施。
还须防止门控后 phase 等待越过短回环的首个 RX 边沿。若采用 origin 有效 phase 为零的
前置方案，必须显式区分 requested offset 与 effective phase，并同步固件提取、SD schema、
宿主解码及旧数据兼容，不能静默改变 offset 的含义。

新双槽位实现采用独立 origin TX、RX 到 forward TX 以及公共采集时基，旧路径不用于新测量。
真实完整 MARK 码字首先完成首组边沿 CS-to-DATA 诊断，待 TX 确实排空后再恢复；
不能用 P3 的普通脉冲冒充 MARK，不能把 origin RTT 当作 follower residence，
也不能把 CS-to-DATA 结果送入多板 CS-to-CS 生产矩阵。每个固件切片仍须新 build、OTA、
同版本 TDMA 短帧闭环和独立复核后，才进入下一切片。

### 真实双槽位 MARK 完整采集（本次快照，非事实源）

专用 MARK persona 同步启动 origin、forward 和公共采集 SM，GPIO 26/27/29/24 分别提供
真实 t1/t2/t3/t4；origin 发送完整码字，forward 由 PIO 实时响应，并非复制同一端点快照。
首切片 build `20260921040801` 已完成全部码本首边沿正测、idle-high 负测与 TDMA 恢复；
`marker-dual-20260921-after-tdma/summary.json` 的 formal 标志为 true，但只证明该轮。
首轮工具未识别数字 START 回应的失败保留于 `marker-dual-20260921-normal-r1/`，修复位于
公共 SCPI reader，未通过延长超时掩盖。

当前完整采集使用 `TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS` 私有缓冲直收 DMA，
原普通 P3 的 `TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS` 不变。Core1 按有界步长解码，
Core0 下载使用 v2 状态的 `capture_generation`；manager 的 pending/accepted request 和
物理 seqlock 共同阻止同 epoch 重启、STOP 或下载期间的新采集混入旧数据。
独立评审发现并修复请求快照读侧 acquire fence；修复后再构建、OTA。

| 实测 | 结果 | 原始证据（均位于 `out/tdma-budget/`） |
|---|---|---|
| 新版 OTA 与 TDMA 基线 | OTA 通过；数据闭环通过，formal=false | `marker-full-20260921-ota-r1/summary.json`、`marker-full-20260921-newbuild-tdma/summary.json` |
| 完整 MARK，四种码本 | 合计 12/12 通过；四通道完整 321 位均零逻辑错误；CRC、反码及 EOF 有效；未补合成前缀 | `marker-full-20260921-codebook0/`、`marker-full-20260921-codebook1/`、`marker-full-20260921-codebook2/`、`marker-full-20260921-codebook3/` |
| 完整缓冲 idle-high | 3/3 预期拒绝，真实采样无下降沿；不是正常码字通过 | `marker-full-20260921-idlehigh/summary.json` |
| MARK 后 TDMA 恢复 | 数据闭环通过；formal=false，不得关闭 SB-RISK-03 | `marker-full-20260921-after-tdma/summary.json` |
| 普通 Latency 回归 | 两组信号、10/25/30 MHz 共 18/18 通过，delay=52 ns；30 MHz 仍为 limited | `marker-full-20260921-latency-regression/summary.json` |
| Latency 后最终 TDMA | 数据闭环通过、bad/overrun 增量零，formal=true；只代表该轮，不覆盖前两轮 false | `marker-full-20260921-final-tdma/summary.json` |

本次去程 52..56 ns、回程 52 ns、residence 24 ns；它们包含 pad/收发器/PIO 端点延迟，
不是纯网线传播时间。码本 0 的原始采样 distance 最大 19，逻辑位仍零错；保留采样量化
差异，不宣称所有 pad 波形逐采样相同。独立复算确认这些差异均在期望转换沿的一采样范围内。
宿主从调用 START 前至 terminal 状态返回耗时 0.812..0.828 s，含 START 请求/ACK、状态轮询
与分拍解码，不含原始数据下载；从 START 响应返回起算为 0.735..0.765 s。
固件采集超时不等于整个下载或解码的端到端时限。
`production_training_certified=false` 保持不变：本切片不是生产 phase 参数、T0 全部验收或完整 P3。

### SCK 独立发射偏移与查询并发修复（本次快照，非事实源）

`CAL:SCK:LOOP:START` 使用独立 SCK origin、forward、公共采集 SM，scope 与 MARK 分离。
GPIO 25/28/29/24 为实测四端点，完整码字与 idle-low 负测均保留原始采样；
launch offset 只改变发送 guard，不改变接收采样相位。
`destination_phase_trained=false` 与 `production_training_certified=false` 必须保持。

build `20260921060150` 首次扫描通过，但 idle-low 第两轮状态查询超时，记录在
`out/tdma-budget/sck-dual-20260921-idlelow-r1/`。修复前错误队列的 `-200` 保留于
`out/tdma-budget/sck-snapshot-20260921-preota-status.log`。原因是完整解码扫描和除法
持有快照写锁，读侧有限重试耗尽。修复将 Core1 私有累加留在锁外，只短时发布
PREEDGE 标志和端点结果；没有增大重试次数或在 host 静默重试。

| 当前 build 实测 | 结果 | 证据（`out/tdma-budget/` 下） |
|---|---|---|
| SCK 四码本，各三档发射偏移、三次重复 | 36/36 通过，完整码字及四端点、generation 均校验 | `sck-snapshot-20260921-codebook0/` 至 `sck-snapshot-20260921-codebook3/` |
| idle-low 故障 | 6/6 预期缺边沿拒绝，无 STAT 超时，最终错误队列为零 | `sck-snapshot-20260921-idlelow-r1/` |
| MARK 完整采集回归 | 3/3 通过 | `sck-snapshot-20260921-marker-regression/` |
| OTA 后、训练后 TDMA 恢复 | 数据闭环通过，两轮 formal=false，风险仍 OPEN | `sck-snapshot-20260921-newbuild-tdma/`、`sck-snapshot-20260921-after-tdma/` |

联合软件回归 471 项通过；新增超时必须失败并清理的测试后 SCK host 74 项通过。
独立 reviewer 的 349 项回归通过；逐采样交错 getter 测试证明解码扫描不再持长锁，
不能证明真实双核所有短发布竞争绝不失败。板测结果也只覆盖上述有界样本。
受跟踪源码 build 前指纹为 `282d88be6185f5e7a266bebfbc506153492d862ec18e2f371f634319f7adc3ba`
（1223 文件）；补丁 `sck-snapshot-20260921-source.patch` 不含未跟踪 host/测试文件。

下节实际接收 PIO 相位加载沿用以下基准。用户指定当前夹具 Latency 的 52 ns 除二为 26 ns；
统一 `phase_delay_samples` 在 4 ns 网格取整为 7 samples、28 ns，必须保留 +2 ns 量化差。
`calibration_training_sck_map_offset_to_phase_cycles` 另加一拍固定流水线，不能把该拍
混作链路目标。接收 offset 必须独立测得，不继承 MARK 的相关误差。前序值及 UID/build/
配置身份需要在同一次新采集中绑定，旧报告不能事后补造当前 generation。

TRN-03 宿主诊断入口已加强原始 transcript、START、终态、generation 分块、STOP 与最终
身份绑定；DATA evaluator 尚缺时必须拒绝下发，不生成 active candidate 或生产矩阵。

### 实际接收相位扫描与新采集残差（本次快照，非事实源）

首版 RXPHASE build `20260921074145` 在配置阶段误报 RESOURCE，证据保留于
`out/tdma-budget/rxphase-20260921-session-r1/`。根因不是已证明的 PIO 空间不足：
RP2350 SDK 的 `PIO_INSTR_MEM0_ACCESS` 为 `WO`，`instr_mem` 是只写寄存器；旧实现
在配置和冻结时读取它，得到零后误判指令不一致。原 host double 模拟可读 RAM，漏掉此硬件限制。

修复后 `tdma_pio_spi_sck_train_sink_program_init` 返回实际送入寄存器的编程值；
RXPHASE STAT v2 的对应字段改称 `programmed_delay_instruction/cycles`，禁止称作硬件读回。
单轮 `phase_loaded` 仅表示编程状态一致；只有从原始完整码字复算扫描斜率后，
`phase_effect_verified` 才为 true。测试覆盖寄存器读零/无意义值，不再依赖假读回。
联合固件 FSM、RXPHASE、session、SCPI 软件回归 529 项通过。

构建中断后自动生成的 `project_build_info.h` 出现全零内容，编译失败日志保留于
`rxphase-wo-20260921-build-r2.log`；通过仓库生成器重建该产物后，Release 构建及
app/A-B/boot 的 Flash 链接检查全部通过。未修改源码以绕过编译错误。

`out/tdma-budget/rxphase-wo-20260921-session-r1/` 在同一 UID/build/配置下依次完成：
Latency 两组各三次均为 52 ns，自动派生 base=26 ns、量化 28 ns；完整 MARK 三次通过；
两条物理路径、三个 offset、各三次的 RXPHASE 扫描全部通过。offset=-1/0/+1 对应
lag=121/120/119 samples，证明采样相位实际移动；两路径均选出 -1 sample，重新装载后
以新 epoch/generation 各采三次，六次残差均为零。各阶段绑定前序 SHA、完整配置身份、
原始 transcript 和错误队列。idle-low 六次均按预期超时拒绝，证据为
`rxphase-wo-20260921-idlelow-r1/`。

路径 0 为 GPIO25→28 的 SCK，路径 1 为 GPIO29→24 的 DATA 回程；后者不是第二根 SCK。
`pipeline_calibrated=false`、`trn_accepted=false`、`trn_training_complete=false` 保持不变：
上述结果只关闭实际 RXPHASE 扫描/重载残差切片，不能替代 accepted MARK/residence 矩阵、
独立 DATA 码字窗口或 TRN-03 矩阵实际装载及正式反馈。参数未 SAVE Flash。

构建时验收源码指纹 `f87e89389fb65a1b7bc1b0b15f87fb0d5232a1b736472d7e1083ecc7ad383f4f`
（1227 文件，快照）；`rxphase-wo-20260921-source.patch` 只包含 tracked diff，
新增 host/测试仍须与补丁一并交接。

新增码本 0/2/3 各 18 次扫描通过，与 session 的码本 1 合计 72 次；证据为
`rxphase-wo-20260921-codebook0/`、`rxphase-wo-20260921-codebook2/`、
`rxphase-wo-20260921-codebook3/`。MARK 回归三次通过，四边沿均为
1088/1140/1164/1216 ns，证据为 `rxphase-wo-20260921-marker-regression/`。
`rxphase-wo-20260921-newbuild-tdma/`、`rxphase-wo-20260921-after-tdma/`、
`rxphase-wo-20260921-final-tdma/` 的数据闭环和 formal 均 true，bad/overrun 增量为零；
有界成功不抵消旧轮间歇 false，SB-RISK-03 仍 OPEN。额外 schema/斜率标志七项测试通过；
它们是 build 后的宿主测试变更，最终设备提交前仍须按最终源码重跑凭证。
文档双检查、十八项文档测试及手动 pre-commit 通过，现有 staged 仅宿主，未放行设备提交。
独立 `rxphase_independent_review` 重跑 530 项并复算原始 word、SCPI 下载、前序/身份
哈希链、Latency 公式及残差，接受本切片进入下一状态。码本 0 回程有 24–57 个采样点
差异，但逻辑位零错、相关峰唯一；不能描述成所有原始采样零误差。

后续 MARK RXPHASE 结果见下节；DATA 将使用真实 CS 锚定的独立码字，不能复制 MARK。
保留完整 MARK 发射且并行 DATA TX/相位 RX 至少需三 SM/三 DMA；如同发次再保留公共
四边沿采样，则需四 SM/四 DMA。旧两 DMA 设想仅在改用 FIFO 预装短 CS 触发时成立，
该触发不等同完整 MARK，不能静默替换。`CAL:TRAIN:STAGE:COMMIT` 会写持久化，
本任务只允许 RAM stage/apply，不调用该命令。以上为实现边界快照，非新冻结契约。

### MARK 独立接收相位（本次快照，非事实源）

`CAL:MARK:RXPHASE` 采用独立 role/persona/scope、独立 PIO 程序及 offsets，CS26→27，
只开 DE32。发送固定高/低/高 origin 前缀、完整原始码字、高 tail；sink 以
WAIT1/WAIT0/WAIT1 建立 MARK origin，只调整接收 NOP。STAT v2 对应 host 字段为
`origin_low_samples`，raw schema 与 SCK 分开；无写专用寄存器读回。

Release build、OTA 通过；验收源码指纹为
`cb84d33c4bf927909a396df3bb6da21c537d3d438e7e4ec0b9d025a3e338231f`（1229 文件，快照）。
`mark-rxphase-20260921-source.patch` 仍不包含未跟踪文件。独立源码审查的 host
400 项、固件 267 项均通过；worker 额外 DATA/PIO timing 合同联合回归 273 项通过。

`out/tdma-budget/mark-rxphase-20260921-session-r1/` 的 45 次采集全部通过：
Latency 六次、MARK LOOP 三次、MARK RX 扫描九次、MARK fresh residual 三次、
SCK RX 扫描十八次、SCK fresh residual 六次。MARK lag 随 offset -1/0/+1 为
121/120/119，选择 -1 后新 epoch/generation 三次 residual 均零；前序 SHA、
UID/build/配置全程绑定，MARK residual 未通过时禁止启动后继 SCK。

码本 0/2/3 各九次扫描通过，与 session 码本 1 合计三十六次，证据为
`mark-rxphase-20260921-codebook0/`、`mark-rxphase-20260921-codebook2/`、
`mark-rxphase-20260921-codebook3/`。`mark-rxphase-20260921-idlehigh-r1/`
三次故障均 reject=TIMEOUT、零采样、TX drained，且清理成功。
`mark-rxphase-20260921-newbuild-tdma/` 和 `mark-rxphase-20260921-after-tdma/`
数据闭环通过，bad/overrun 增量零，但 formal 均 false；SB-RISK-03 再次复现，仍 OPEN。
本切片不建立生产矩阵，不代表 accepted residence、DATA 或完整 TRN，参数未保存 Flash。

### DATA 同次采集切片进度（本次快照，非事实源）

`calibration_data_loopback.py` 同时验证公共 GPIO 波形和相位接收波形：MARK 使用源节点零、
DATA 使用源节点一，各自独立核对完整码字、header/inverse/CRC/EOF；扫描要求公共波形的
路径/响应驻留稳定、DATA lag 随接收 offset 反向变化，并重新采集所选 offset 的残差。
状态中的 PIO 指令值只表示编程值，不宣称对只写指令存储器进行读回。
SCK offset 在 DATA 阶段仅作为前驱证据；此切片尚未把 SCK offset 应用于 DATA PIO。

`calibration_single_board_session.py` 已接入 DATA scan/residual，预期全链六十三次新采集，
接入当时板端仍是上节 MARK build；更新后的板测见下文，尚无该全链成功实测。独立 host 审查发现每轮 DATA 配置
只与同批次比较、缺少与前驱配置直接连接，现已修复：先保存 trial，再将采集实际 CONFIG
与 session identity 比较；缺失或变化立即拒绝，禁止 stage 边界恢复旧值后继续。
缺失/各字段变化及扫描/残差入口共二十六项反例独立通过。
综合宿主回归六百三十三项通过（`out/pytest/data-loop-host-integrated-20260921-r1`），
随后配置绑定修复的 session 回归二百八十三项通过
（`out/pytest/data-loop-session-binding-20260921-r1`）；这些数量有重叠，不相加。
独立 reviewer 接受该阶段宿主切片，随后设备复核和新 build 板测进度见下文。

设备独立审查现已通过，生产 FSM/旧 P3 联合测试三百二十项通过，PIO 合同测试通过；
审查补齐两层 offset 范围及 persona 抢占门禁。首轮编译在 SCPI 回调因误导缩进告警失败，
补花括号后 Release build `20260921092708` 成功，A/B/boot 链接门禁全通过。
日志 `data-loop-20260921-build-r1.log` 保留失败，`data-loop-20260921-build-r2.log` 为成功；
构建阶段精确源码快照
`out/tdma-budget/data-loop-20260921-source-r2.zip` 含被跟踪和未跟踪文件；构建时验收源指纹
`f432d7e83a5c7cd8b05e2bfb839294d1d49fe2733d1bf0a10a29018d3769b547`，计数一千二百三十三。

T3 只读设计审计确认现有 TRAIN stage 要求完整有向环路，且拒绝诊断标志，不能复制单板
仅有的 MARK 零到一/DATA 一到零来填反向 link。后继应使用独立 RAM diagnostic candidate，
经 owner mailbox 装载并绑定配置，保留 TRAIN/Flash 产品门禁。当前 MASTER flight 不消费
MARK phase，T2 DATA 的 CS 基准也不同于 flight capture 的 SCK 基准；简单保存 offset 或
将训练 NOP 原值搬到 flight 都不能证明实际应用。须记录字段消费范围和编程指令，并补
同一生产 PIO 基准上的原始窗口/短帧验证后才可评价 T3；尚未实施该后继切片。

该固件随后已 OTA 成功，`data-loop-20260921-newbuild-tdma/` 数据闭环及本轮 formal 通过。
`data-loop-20260921-session-r1/` 前四十五次 Latency/MARK/SCK 采集通过，DATA 十五次
完整逻辑码字通过，但 strict scan 失败并停止，未下发 residual。epoch 六十二的前向 CS
为十四个采样，其余为十三个，DATA lag 同步增加一个采样；GPIO27 原始码字距离为一，
其他该轮通道距离为零，不能称所有原始距离为零。独立复算发现旧host把采样起点锚定RXCS，
实际sink等待TXCS；全部同次原始数据满足 `t4 - lag - t1 - data_offset` 恒定。
失败恢复记录 `data-loop-20260921-failed-scan-after-tdma/` 数据通过、formal=false，风险仍在。

后续宿主诊断窗口政策将显式 opt-in，strict 默认仍保留，且不把旧报告改写为成功：
每个扫描offset至少三次、候选须有唯一严格多数众数且各组一致、全体候选跨度至多一个采样；
把实际观察到的 `candidate - selected` support 连同配置、scan generation、source residence
和TXCS采集截距锁入政策。重新装载的采集至少三次，原始residual只能位于该support，且零
须为唯一严格多数；全非零、出现未观察过的一侧、跨度扩大或重用证据都拒绝。
`bounded_residual_verified` 与 `strict_zero_residual_verified` 分开，`zero_residual_verified`
只允许全部原始残差为零；所有 training/production accepted 保持false。
这是本次诊断实验新增的有界判据，不将旧工具默认 `max_offset_span` 当作绝对误差合同，
不登记生产契约、不关闭 OPEN 风险。首轮严格候选三/四分歧保留，后续新鲜结果如下。

窗口政策随后经独立审查，补齐原始scan重算政策防篡改及跨scan/fresh源residence绑定。
综合独立宿主回归三百四十五项通过，新增residence变化但残差恰好为零的反例也独立通过；
原始F/B作为测量分布保留，strict只声明原始残差全零，不推断每段物理传播始终不变。
新 `data-loop-20260921-session-r2/` 六十三次全部通过，独立复核全部身份、前驱及transcript
SHA链闭合；十八轮DATA的Node零/一完整码字逐bit精确匹配。主码本一的DATA offset为三，
扫描候选十五次均相同，support为 `[0]`，重新装载三次残差均零，严格默认判据也通过。
MARK offset为负一，两条SCK路径均负一，Latency仍为五十二ns、基准二十六ns。
这些是本轮事实，不抹去首轮一个采样的变化。

| DATA补测（快照） | 结果 | 原始目录（均在 `out/tdma-budget/`） |
|---|---|---|
| CS idle-high、DATA idle-low，各三次 | 各自timeout/缺边沿拒绝符合预期，STOP资源释放通过 | `data-loop-20260921-faults-r1/` |
| 码本零，九次扫描 | 逻辑码字及phase效应通过；候选三/四为五/四，组众数不一致，拒绝且未运行residual | `data-loop-20260921-codebooks-r1/` |
| 码本二，九次扫描 | 逻辑码字及phase效应通过；候选三/四为六/三，组众数不一致，拒绝且未运行residual | `data-loop-20260921-codebooks23-r1/` |
| 码本三，九次扫描及三次重载 | 候选三/四为八/一，各组众数均三；support `[0,1]`，fresh三次原始残差全零 | `data-loop-20260921-codebook3-r1/` |
| gap两端及offset两端 | gap二百五十六/一千各自扫描负十/零/正十、各三次，再fresh三次；两组候选均三、残差全零 | `data-loop-20260921-bounds-r1/` |
| 补测后TDMA恢复 | 数据和本轮formal通过，bad/overrun/magic增量零；不抹去中途formal=false | `data-loop-20260921-after-tdma/` |

码本零/二的拒绝仍属稳定性风险，不自动放宽政策或以再次抽样覆盖失败；不能称全码本TRN通过。

### 正式反馈配对异常复现（本次快照，非事实源）

历史原始数据 `mark-rxphase-20260921-after-tdma/summary.json` 含反馈有效却 RX 早于 TX 的
样本。只读宿主实验 `out/tdma-budget/feedback-pairing-characterization-20260921.c` 编译并
运行当前 `tdma_ring_runtime.c`，复现三种不同状态：新 TX 覆盖旧成对 TX 而保留旧 RX 时，
retention 分支仍报告有效、发布的时间戳却逆序；若同时收到新 RX sequence，相关检查拒绝为
`TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING`；同一合法配对仅自然过期则拒绝为
`TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING`。完整输出在同名 `.log`。
这证明配对发布和保留超时必须分开处理，不证明所有硬件 formal=false 都来自配对问题。
本实验未修改运行时设备代码、未关闭 SB-RISK-03，后续须独立修复、构建和复测。

### 配对修复与新 build 复测（本次快照，非事实源）

上一节是修复前复现实验。随后 `tdma_pio_spi_ring_adapter.c` 的异步 TX completion
只更新对应 sequence/CRC 的历史，不覆盖已由 RX 关联的公开 TX 时间戳；
`tdma_ring_runtime.c` retention 同时保留 TX/RX/RTT 及精度来源。新坏 RX 和原超时
继续拒绝，未延长 timeout。两项真实 C 回归先在旧实现失败，修复后相关五十项测试通过，
独立 reviewer 复核两个 C 回归及消费者通过。

Release build `20260921101225`、A/B/boot 链接检查和 OTA 通过，UID仍为
`839E1AE79EA20F31`。package SHA256为
`DC525FE050171C2DB4A93F2A5645E1744126A8FFB54E0F84CDB0C92A016F5FDE`；
构建时受跟踪源码指纹为
`41e6b818d23b2fc1b084685f2285d69bce0c0115e7b7a8777c7f0174be2d2029`，
计数一千二百三十三；包含未跟踪源码的差异快照为
`out/tdma-budget/feedback-pair-20260921-source-r1.zip`。后续宿主门禁修改不在该构建快照内。
OTA原始证据为 `out/tdma-budget/feedback-pair-20260921-ota-r1/`。

前后两轮三十秒数据闭环均通过，各五十四个采样，原始目录分别为
`feedback-pair-20260921-newbuild-tdma/`、`feedback-pair-20260921-after-tdma/`
（均位于 `out/tdma-budget/`）。独立复核九十七个有效反馈样本的 TX/RX/RTT 全部一致，
未再观察 reason5；但各有六/五个 reason2，RTT按无效状态清零。两轮正式反馈均失败，
SB-RISK-03 保持 OPEN；不能把临时检查脚本的“无错误配对”passed 当作稳定 formal 通过。

新 build 的 `feedback-pair-20260921-session-r1/` 六十三次新鲜采集全部通过：Latency
五十二ns、基准二十六ns，MARK及两条SCK偏移负一；DATA十五个候选均正三，fresh三次
原始残差全零，严格默认政策也通过。独立 reviewer 复核前驱/自身/转录 SHA、身份和
配置链，并逐bit重算十八次DATA原始采样通过。此结论仍仅为诊断前驱链，未应用生产矩阵。

宿主 `trn03_single_board.py` 增加 `raw_feedback_pair`：从原始字段重组时间戳，核对
字段宽度、非零TX、RX不早于TX、差值等于RTT、timeout、精度与硬件来源标志。
允许最新TX序号领先已配对RX，不假定公开SCPI包含未发布的关联sequence/CRC。
独立一百六十五项回归通过，包括首/中/尾配对损坏与跨低字回卷；两轮实板原始正式门禁
均正确拒绝。该门禁不代表 T3 candidate/application 已实现。

生产PIO同时波形首次采集 `feedback-pair-20260921-flight-wave-r1/waveform/` 有效，
但外层辅助脚本把清理命令缩写成不支持的 `CAL:TOPO:PROB`，产生负一百一十三错误；
该失败保留。随后用完整 `CALibration:TOPology:PROBe 0` 清理确认返回 `0,0`，错误队列清零。
修正辅助脚本后的 `feedback-pair-20260922-flight-wave-r1/` 采集及STOP/probe清理通过。
原始SD分段及校验、六路公共采样、设备build和schedule均已保留。采样有效且坏帧不增长
不代表正式反馈连续或TRN已完成；仍须对照生产PIO的实际SCK/DATA采样相位及处理耗时。

### real-flight 对照与启动时钟切片（本次快照，非事实源）

用户要求参考更完整的 TDMA 分支，已 fetch 并只读对照
`origin/wip/tdma-real-flight-processing`，本次 tip 为 `2c897840`，未切换当前脏工作区。
该分支的优先RX、冻结几何及自主release有跨域依赖，且移除当前单板local-return路径，
不能整文件覆盖。其验收凭证不能授权当前源码；分支中的quick流程、严格质量和预算
证据也分别保留失败。候选RX两拍流水修改暂未实施，优先复用已独立验证的基础修复。

`feedback-pair-20260922-profile-r1/` 重置计时器后采集三十秒，共一百八十四个状态，
reason零/二分别一百七十/十四；有效反馈配对均一致。RX观测最大间隔为四点七六二四七六ms，
完整TDMA阶段峰值为一点七零五七八ms；当前软件周期一点五ms、TDMA预算八百五十us、
链路反馈期限二ms。计时记录显示 clamp 四千九百一十三次，跳过二十五万五千一百一十四word，
无epoch/stale-hint/copy拒绝。原始波形离线解码见
`feedback-pair-20260922-flight-wave-r1/waveform/decoded-flight.json`：双向同一sequence
二百六十八，在SCK上升沿后正三十二至正九十六ns的九个离线窗口均原样CRC通过，
无bit修补。这不是实际PIO offset扫描/应用，不据此选择生产相位或关闭T3。

源码两拍路径为READY只accept、下一拍才capture/request，与profile中的idle/ready约一比一
一致；但runtime在新RX到达时先更新证据，所以理想交替并不必然超时。必须结合REQUESTED/
BUILDING和真实服务延迟定位reason2，不能把周期算术直接当完整根因。

先移植 real-flight `6c8b0b41`：`board_clock_init()` 在 `app_runtime_init()` 内核启动前
固定系统时钟，后续 `board_init()` 幂等调用；移除当前分支晚期手工SysTick重绑定，
FreeRTOS port继续拥有SysTick。保留当前TIMER1精确周期和临时预算。旧build已有晚期补偿，
因此此项是启动顺序修复，不能预先断言它解决steady反馈过期。生产函数可执行回归覆盖
SDK失败、错误读回、内核/任务失败、先时钟后内核、重复幂等，另检查板级/内核频率一致。
相关六十二项通过、十四项为另一时钟实现而跳过；新增配置一致性检查随后通过。
独立reviewer接受源码切片，Release/OTA及实板结果续记于后。

## OPEN 风险与下次提交的退出条件

| 风险 ID | 当前事实 | 关闭所需证据 |
|---|---|---|
| SB-RISK-01 | 完整单板 P3 链未跑完；MARK 四边沿/完整码字、SCK 实际 RX 相位及重载残差已有切片证据，accepted MARK/residence 矩阵、DATA 窗口及 TRN-03 未闭合；旧 MARK 起点缺陷仍不能作为时延证据 | 按真实并行 TX/RX 端点完成 MARK/residence/SCK/DATA 及后继流程；每次状态切片先完成新 build TDMA 短帧闭环；全链原始成功证据，不允许复制快照冒充第二节点 |
| SB-RISK-02 | 单板夹具窗口仅暂定实测基线；修改后 HIL 已通过该窗口，但独立计量与端点 bias 未闭合 | 对实际 10 cm 夹具重复测量、保留四边沿及量化/端点 bias 解释，证明单板窗口合理；四板 5 m 范围与故障负测不变 |
| SB-RISK-03 | 短帧正式反馈间歇 false；SCK RXPHASE 修复 build 三轮 formal=true，MARK RXPHASE build 前后再次 false；已有 quarantine/schedule miss 及 TX/RX 时间戳配对异常线索 | 定位正式反馈缺失与调度诊断，复跑并保留实时门禁、短帧和原始波形；不能以数据计数前进或单次 true 替代稳定 formal gate |
| SB-RISK-04 | 新 P3 role/端点及重复 STOP 修复仍在未提交固件，远端 host 单独拉取不等于板端 build | 恢复换机包、复核差异、全量构建并 OTA，确认 UID/build；设备源码提交前取得与最终源码一致且仓库认可的硬件凭证；不改 hook 规避 |
| SB-RISK-05 | P3/legacy loopback 共享 persona 的交叉 STOP 资源归属尚未负测；已修的仅是 idle P3 STOP 误动其他 persona | 检查活动/部分装载/恢复状态下交叉 STOP 所有权，补生产 FSM 负测和硬件反复切换，无资源泄漏或错误卸载 |
| SB-RISK-06 | 先前 TDMA/VDC 预算、Flash 显式保存、GUI、USB 切换改动尚未随本次提交 | 将补丁与已有测试恢复；核对 500 us/1 ms、10–30 MHz GUI 档位、RAM APPLY/显式 SAVE、CDC↔USBTMC；按最终固件重验并纳入设备凭证 |

关闭所有 OPEN 项后，更新本表的结果和证据，由独立 reviewer 复核，才可创建下一批提交。
若单板仍不能满足仓库设备凭证范围，继续准备所需多板验收；单板诊断成功不能冒充四板 P3。

## 换机包及恢复

计划本地包：`out/handoff/single-board-p3-20260920.zip`（尚未生成）。**换机前必须生成并转移；git pull 不含未提交固件。**
包内 `manifest.json` 记录导出时 HEAD、分支、未提交文件列表和 SHA-256；`remaining.patch`
保存相对最终有限提交 HEAD 的全部被跟踪工作区差异，`untracked/` 保存未跟踪源码/文档。
`evidence/` 保存本次 `out/tdma-budget/` 原始证据，`firmware/` 保存当前 OTA 包、构建输出和缓存。
补丁不属于硬件验收放行或已提交固件。保留原工作区与失败证据，不使用 reset/clean。

在新机拉取本分支后，先确认 HEAD 等于包内 manifest 的基线；若已有新工作，不覆盖，先建独立工作树。

```powershell
git switch feature/node-sequence-reservation-trigger
git pull --ff-only
git config core.hooksPath .githooks
# 将换机包解压到 out/handoff-import 后，核对 manifest.json 和文件哈希。
git apply --check out/handoff-import/remaining.patch
git apply out/handoff-import/remaining.patch
# 将 untracked/ 内文件按相对路径恢复；若目标存在，先比对，禁止直接覆盖。
# 将 evidence/ 内容恢复至 out/tdma-budget/；firmware/ 恢复到原 build 相对路径。
git status --short
```

需要新构建时保持 `PROJECT_ENABLE_USB_RUNTIME_SWITCH=ON`。旧构建缓存含本机绝对路径，
新机应选择新的 build 目录重新配置，不复制缓存继续构建。旧包可作复现实物，不能代替新源码验收。
原约定 Git Bash 路径不可用，本次构建实际使用 `C:/Program Files/Git/bin/bash.exe`；Python/串口命令原生 PowerShell 执行。

恢复并检查设备后，重启验收链的入口如下；新 build 必须改用其对应 package/OTA summary，不能混用旧证据：

```powershell
python tools/hardware_acceptance/p3_hardware_acceptance.py resume --config config/hardware_acceptance/p3_bench_single.json --package out/build/marker-full-20260921-r1/DHRT100_UPDATE.pkg --ota-summary out/tdma-budget/marker-full-20260921-ota-r1/summary.json --receipt out/tdma-budget/next-single-receipt.json --out-dir out/tdma-budget/next-single-acceptance
```

## 软件验证与独立审核

有限 host 提交已在只含该提交差异的独立工作树验证，相关测试 144 项通过；未借用未提交
设备源码通过宿主测试。P3 固件生产 FSM 67 项独立复核通过，但该固件仍须走 SB-RISK-04。
文档遵守自回归检查；不新增契约登记、不修改任何契约状态。
独立 reviewer 已复核 coarse/coded 与 idle P3 STOP 修复；夹具配置及本交接记录在本次推送前交叉审核。

本次 MARK 独立复核发现并修复准备/完成身份绑定缺口：两阶段均核对 requested base、offset、
fault flags 和 tick，完成阶段再绑定准备阶段的 topology generation、topology/profile/schedule
CRC 与 marker CRC。正负变异用例与正常/故障 HIL 均覆盖；没有放宽多板规则。
`coarse_coded_review` 独立复核通过基础相关性诊断切片，但明确不关闭双端 MARK 或任何整链风险。
最终 MARK/waveform/SCK/DATA/P3 宿主测试合计 181 项通过，文档测试 18 项通过（本次快照）；
命令及临时目录为 `out/pytest/p3-mark-final-20260921-r3`、`out/pytest/p3-marker-docs-20260921-r2`。
文档双检查与手动 pre-commit 通过；当前 staged 仅宿主源码，hook 跳过设备门禁，不是设备验收通过。
上述 181 项及未 OTA 描述属于旧基础 MARK 切片，不能代替当前完整采集证据。
当前设备源已修改并重新 OTA；读侧 fence 修复前联合回归 387 项通过，修复后独立
FSM/MARK/serial 回归 216 项通过，其中固件 136 项。交接记录按文档自回归技能维护，
不登记临时诊断为契约。尚未提交或推送，保留先前暂存范围；参数未 SAVE Flash。
`coarse_coded_review` 已独立重算本轮全部原始 words、各通道码字/CRC/EOF、分块 generation、
idle-high 无边沿、四时间戳公式及 Latency 结果，接受完整 MARK 诊断切片；不关闭整链 risk。
文档双检查、18 项文档测试及手动 pre-commit 通过；仍为仅宿主 staged，设备门禁未放行。
修复后的受跟踪验收源码指纹为 `06f9446f4e993decb2b9cb4def2d82f99ff60ba3c4072ab84834d9bb3cf4544e`
（工具计数 1219，快照），补丁 `out/tdma-budget/marker-full-20260921-r2-source.patch` 仅包含
tracked diff；新增 MARK 工具及测试仍是未跟踪文件，换机必须一并携带，不能只复制补丁。
