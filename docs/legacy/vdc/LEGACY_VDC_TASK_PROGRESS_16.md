# VDC 任务进度归档 16

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_16.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/legacy/vdc/` 其他归档分卷
Last updated: 2026-10-25

本文是 `docs/vdc/VDC_TASK_PROGRESS.md` 按 C14 轮转出的最旧条目（逐字迁移，倒排）。

## 归档条目
### VDC-PROGRESS-20260922-041：内部误差预算定位与保守窗口关联修复

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本批未提交，不改固件、Core1 探针、实时席位或物理精度契约。以下数值为实验快照。
- 离线分解 `p3-nonintrusive-correlation-v3-r3` 与 `p3-one-second-correlation-v4-r4`：三个 follower 的末次 MATCH 宽度均为 172 ns，其中远端编码区间 120 ns、本地投影 52 ns；本地原始十三个 250 MHz tick 同为 52 ns，没有几十纳秒的坐标舍入膨胀。r4 稳态 bin 最大宽度分布为 176..208 ns。路径 delay 只是暂定 reverse-DATA 转置标量，平移区间而不传播独立物理路径不确定度；后者未知，不是零。原件哈希与可复算脚本见 `out/precision-budget-20260922/width-budget.json`。
- 按 ROI 回收一秒 summary/GUARD 实验，保留 QPC 时钟证据及十秒窗口。独立审核确认现有 DMA/CPU enable anchor 已把高字读取移出低字包围区间，没有可安全直接删掉的读数；未做扣固定延迟、以中点替代端点或扩大 DMA/PIO 资源的伪收窄。下一核心候选是 NO1 原始 DMA enable 区间，但仍须先证明描述符与硬件排序方案，不以源码猜测承诺收益。
- 当前 build `20260921180908` 的四板 OTA、校准与 TDMA 证据位于 `out/HardwareAcceptance/20260922/p3-ten-second-qpc-v4-r1/`。DPLL 首次因自定义收据路径未传入 manifest 而在采样前失败，后从交接点单独恢复六十秒 scope-on，不重新递归 P0T。`dpll-recovery/report.json` 通过，清理/恢复错误为空。
- 独立审核进一步发现旧 scope 关联用写入返回时刻作触发下界，遗漏写入期间触发及 RAW 负时间段。仅在 host 离线复算改用命令 start 加 RAW 最早负偏移，终点取其后首次 STOP 回复结束；原始命令、旧采样字段和 RAW 全保留。修复后 162 项定向测试通过，独立复核 PASS。真实 RAW 复算外部最大相差 41.899 ns、三从各十个稳态唯一 bin、零歧义、RUN 查询零；内部包络仍分别为 `[-217,+102]`、`[-121,+101]`、`[-215,+108]` ns。外部 INFO 与内部超百纳秒现象分别保留，不相互覆盖。
- `corrected-scope-summary.json` 明确是旧采集的离线再分析、不是当前源码 P3 收据。随后 `p3-ten-second-qpc-v4-r2/` 通过复用同固件 package/OTA 的 `resume` 完成当前 host 源码 P3，source fingerprint 为 `f5b2d5cb15880a00c216aaafb8117cd446d19664edaf03e555ee9bad93663100`，结果 PASS_WITH_WARNINGS，`check-staged` 通过。三从仍各十个稳态唯一关联，RUN 查询零；外部最大相差升至 172.231 ns，十二窗均超百纳秒并告 WARN，内部包络为 `[-149,+107]`、`[-149,+104]`、`[-142,+104]` ns。两轮差异保留，不用较好轮次覆盖当前偏差。
- `exact_event_correlation_qualified`、`numeric_error_qualified` 均为 false；本批证明关联下界更可信，未证明 GPIO 物理精度或跨次复现性达标。下一 gate：停止重复硬件回归，先比较 r1 定向恢复与 r2 的配置、原始 enable 区间和路径/输出读回，再选择最小核心精度实验；无需因 WARN 向 P0T 递归。

### VDC-PROGRESS-20260922-042：缩短输出起点软件区间的核心实验

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本批未提交，改动仅在 SYNC_IO 既有 RUN owner 内，不增加 PIO/DMA、IRQ 屏蔽、实时席位或板端观测查询。以下数字为实验快照。
- 最近边界定位：前两轮 build、PI、output timing 和 output delay 读回一致；外部偏移集中在 NO4，中位数从 -8.022 ns 到 +149.934 ns。NO4 RUN enable 包围区间从 960 ns 到 2004 ns，其中 before→observed 为 796/1336 ns，observed→after 为 160/664 ns；路径标量差异只有数纳秒。它们是相关证据，不把区间增宽直接换算成真实输出延迟。逐板对比及原件哈希见 `out/precision-budget-20260922/run-comparison.json`。
- 核心改动：`sync_io_run_output.c` 在最终 raw 采样前完成非触发 DMA 地址配置、FDEBUG 清理和 paired 高段预装；first-low 仍按最终 sample 重编码，然后触发剩余 DMA、enable SM。取消、时钟、溢出和 deadline 失败继续异步退休，FIFO/workspace 不提前释放；单脉冲不触发零长度 DMA，后续 refill 和完整 observed→PC→after 原始证据保持不变。
- 第一版只把 duration 局部计算写到 raw 采样前，实际 Release 仍把 `__aeabi_uldivmod` 下沉进计时区间，未部署。最终改为 Core0 STOP prepare 计算不可变 `s_duration_ticks`，随既有 PREPARED release/acquire 交接，Core1 只读取。A/B 实际反汇编均确认最终 before read 到 enable 之间无 duration 除法调用；反汇编及决策记录位于上述 out 目录，不用 C 源行序冒充执行时序。
- 最终 host 定向回归 334 passed，命令为 `python -X utf8 -m pytest tests/python/test_sync_io_run_output.py tests/python/test_sync_pulse_uniform.py tests/python/test_vdc_run_output.py -p no:cacheprovider --basetemp out/pytest/run-origin-preload-r4 --junitxml out/pytest/run-origin-preload-r4/results.xml`。覆盖 paired/uniform 预装耗时后重编码、预装取消/deadline 无触发、FIFO 前缀、单/多脉冲、读数反向/回绕、连续与有限租期。r1/r2 测试编写错误原件保留；独立代码及所有权复核 PASS，Release 构建通过。
- 同指纹 P3：`out/HardwareAcceptance/20260922/p3-run-origin-preload-r1/` 使用 build `20260921184756`，source fingerprint `b528ab86ad4671fc681792b2d0583fd714b532a41918f637bb707ad524aa857c`；quick P3 为 PASS_WITH_WARNINGS（INFO/WARN/ERROR/FATAL 为 33/18/0/0），`check-staged` 通过。四板输出起点区间为 516/536/520/516 ns，对比此前通常约 960 ns 确实收窄；外部最大相差 49.958 ns、INFO，三从各十个稳态唯一关联，RUN 查询零，三从末次 MATCH 宽度仍为 172 ns。内部稳态包络为 `[-210,+100]`、`[-199,+100]`、`[-118,+105]` ns，保留其超百纳秒现象，不用外部较小值覆盖。
- 按 ROI 保留已兑现的输出起点改动。同固件/校准/补偿的一次 STOP/ARM 联合复采已通过，`joint-repeat/report.json` 耗时 89.219 s，errors/cleanup_errors/restore_needed 均为空；未重复 P3 构建、OTA、拓扑或训练。四板输出起点区间均为 512 ns，外部原始相差绝对值最大 35.865 ns，三从各十个稳态唯一关联，原生 trace/分页与 RAW 离线复算通过，末次 MATCH 区间仍为 172 ns。两次改动后窗口均兑现区间缩短，不据两个有限采样窗宣布长期复现性或物理精度完成；复算原件见 `run-preload-repeat-absolute-summary.json`，对比见 `run-comparison.json`。
- 复采暴露独立 P3 host 缺陷：`phase_max_abs_ns` 原实现取带符号最大值，把此轮负向极值 -35.865 ns 摘要成 +8.219 ns，可能漏报负向超限。修正为全部值先取 abs 再求 max，保留旧摘要，不修改波形。八个正负边界用例覆盖 ±100/101/1000/1001 ns 和全负窗口，最终 P3 DPLL/alarm 定向回归 170 passed，独立复核 PASS。本次两种数值都为 INFO，但这是数值摘要修复，不用“告警没变”掩盖错误。
- host 告警修复的同指纹验收已完成：`out/HardwareAcceptance/20260922/p3-phase-absolute-r1/` 复用上述 package/OTA 并核对四板在线 build，source fingerprint `77aa8fe516bf0c218d580ad21c6777ae6523eb2e4bf27f087e1e75abbd1324fa`，quick P3 PASS_WITH_WARNINGS、`check-staged` 通过。外部最大绝对相差 34.335 ns、INFO；内部包络为 `[-124,+101]`、`[-130,+109]`、`[-123,+106]` ns，三从各十个稳态唯一关联，RUN 查询零。outer 耗时 86.515 s，清理与恢复无错误；没有重复构建或 OTA。
- 下一 gate：停止本批硬件运行，保留已兑现输出起点优化及对称告警修复；内部 TX/MATCH residual 区间尚未缩小，物理路径不确定度及逐事件数值资格仍未闭合。继续原始参考/本地观测的误差预算与校准方案，不改阈值、不拟合输出补偿来冒充监控精度。

### VDC-PROGRESS-20260921-040：双 STOP 仿射锚建立无扰 bin 关联

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮只改 host 证据关联和 P0T host 判定，不改固件、VDC owner、Core1 工作或实时席位。实现提交 `a7eb455c`，短闭环 schema 为 `HAOFV_P3_DPLL_SHORT_V3`。
- internal-seal 在四板全局 START 前、最终全局 STOP 后各取一次既有 TIMER1 bridge，原始 action 证明所有 pre 锚在最早 START 前结束、所有 post 锚在最晚 STOP 后开始；RUN 内板端查询为零。前后 clock identity、原生 trace tick、scope trigger host timestamp 和哈希原件必须一致，离线用全部仿射角点保守投影，防止中心点关联掩盖边界歧义。
- 结果只把外部窗口关联为唯一十秒 bin、歧义、capture 外或未映射；它不是逐事件身份，也没有证明内部 residual 与 GPIO 相差的数值误差。`exact_event_correlation_qualified=false`、`numeric_error_qualified=false` 保持显式；quick debug 中关联证据不足报告 false/WARN，显式 strict DPLL 才升级，符合新增诊断不得拒绝基础调试验收的执行约束。
- P0T 同轮确认只检验唯一物理线序。未校准前帧可能偶然对齐也可能完全不对齐，所以 frame/magic 只作诊断；r3 十二个有向组合的 `rx_frames` 全为零（快照），但 `rx_words` 活动仍唯一给出 NO1→NO2→NO3→NO4→NO1 并 PASS。r1 首链已由 `rx_words_delta=13696`（快照）正确识别，实际失败来自 STOP 返回兼容 ACK 后 helper 未接受；现在两个允许 ACK 都必须再由新鲜 STOP 状态读回确认。
- 软件最终依赖闭包 655 passed / 1 skipped（快照），唯一跳过为 Windows symlink 权限；Python 编译、工具治理、whitespace、`check-staged` 与 pre-commit 通过。r1 失败、r2 早期 staged 中间轮均保留，不作为最终凭证。
- 最终 r3 原件为 `out/HardwareAcceptance/20260921/p3-nonintrusive-correlation-v3-r3/`，build `20260921141824`，四板 scope-on P3 为 `PASS_WITH_WARNINGS`（快照：INFO 33 / WARN 19 / ERROR 0 / FATAL 0）。外部最大绝对相差 39.165 ns；三个 follower 各十一窗在 capture、十个稳态窗唯一关联，due=60 的一窗在 capture 后，歧义为零。最坏仿射角点速率偏差约 120.890/249.935/112.500 ppm；内部稳态 residual 为 `[-114,+100]`、`[-113,+102]`、`[-119,+105]` ns，最大 119 ns，均为本轮快照。内部按实现阈值告 WARN；既有 TDMA/时长反馈保留，DPLL 关联无 evidence error。
- 下一 gate：继续 `VDC-PRECISION-001`，缩小 host/TIMER1 仿射包围不确定区间或引入同事件标识，使 `numeric_error_qualified` 有可审计的关闭路径；同时继续 `VDC-OBS-007` 的质量发布和恢复对账。当前结果不授予 formal lock、连续 GPIO 精度或产品 RUN。

### VDC-PROGRESS-20260921-039：外部 HDO 对比下的内部稳态包络复验

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮由 tools 离线 decoder/analyzer 分类迁移触发同指纹四板复验，不改固件、VDC owner、Core1 工作或特等席。当前目标收紧为外部示波器对比前提，scope-off 结果不能单独关闭内部精度 gate。
- 实现提交 `fc609c64` 将 observation binary decoder 与 residual analyzer 迁入 VDC 标准目录，活跃 capture/monitor import 和 joint provenance 全部切换到 canonical；旧入口兼容、原件归档。decoder 的 CRC/schema 拒绝和 analyzer 的 metadata/坐标域/decimation/lock 声明边界保持不变。
- 软件与复核：最终聚焦依赖闭包 242 passed / 1 skipped，唯一跳过是 Windows symlink 权限；完整回归按用户反馈在无失败至 24% 时停止，不宣称本批全量通过。独立复核先指出外部 cwd 未固化一项 P2，补新旧成功/失败 CLI 的仓库外运行后最终 PASS；治理器 351 owned/visible files 闭合（快照）。
- scope-on P3：`out/HardwareAcceptance/20260921/tools-dpll-offline-evidence-r1/`，build `20260921115305`，HDO serial `HDO4A244301137`，结果 `PASS_WITH_WARNINGS`（快照：INFO 33 / WARN 18 / ERROR 0 / FATAL 0）。外部十二窗、四十八个 RAW block 均绑定收据并离线复算；最大绝对 GPIO 相差 40.151 ns，全部窗在 ±50 ns 内，外部分级 INFO。
- 同轮内部对照：NO2/NO3/NO4 约五十秒稳态 residual 包络为 `[-117,+110]`、`[-114,+101]`、`[-115,+104]` ns，最大 117 ns；末态 endpoint 最大仅 97 ns。原生分页重组、完整六十秒 coverage、成功间隔、共同 session/generation 与末 bin/endpoint 均通过。内部 extrema 继续比 endpoint 更能发现窗口内超 ±100 ns 的偏离。
- 证据边界：外部 GPIO 稀疏窗与内部 owner residual 的量、时间原点和采样时刻不同，不能逐事件相减或把 117 ns 与 40.151 ns 的差声明为监控误差；本轮只证明两条链在同一四板会话各自完整、可复算，并给内部保守包络提供外部物理对照。WARN 来自既有 TDMA 质量反馈，DPLL 外部相差为 INFO，不提升 formal lock、连续物理锁相或产品 RUN。
- 下一 gate：继续 `VDC-PRECISION-001`，建立内部 bin 与外部窗口的显式时间关联后才能量化内部监控误差；同时推进 `VDC-OBS-007` 的质量发布/恢复对账。未具备同事件关联前，不用两个 envelope 的数值差调参或授予精度。
