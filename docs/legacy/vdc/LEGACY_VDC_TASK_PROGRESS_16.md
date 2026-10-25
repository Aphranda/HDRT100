# VDC 任务进度归档 16

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_16.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/legacy/vdc/` 其他归档分卷
Last updated: 2026-10-25

本文是 `docs/vdc/VDC_TASK_PROGRESS.md` 按 C14 轮转出的最旧条目（逐字迁移，倒排）。

## 归档条目
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
