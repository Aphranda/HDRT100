# VDC ?????? 03

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-18

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
