# 引导/测量载波分离设计（10K 引导 / 10M 测量 / T3 后 10M 常驻）

Status: Draft
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_GUIDE_CARRIER_SPLIT_PLAN.md`
Related: `docs/calibration/CALIBRATION_TASK_PROGRESS.md`, `docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`, `docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`
Last updated: 2026-11-09

## 1. 目标口径

阶梯的动作是 **C(配置) → T(触发) → R(测量) → Q(回读) → S(提交)**：

- **C/T/Q/S 走 10 kHz 引导**（level 23，150 ms/rev）；
- **R 段用 10 MHz 测量**（raw PIO 突发，不是环的载波）；
- **T3 成功之后**，10 MHz 的 TDMA 环才**常驻**（产品运行态）。

当前代码在 R 段把**环**切到测量档（P2 在 `P2_STOP`、P3 在 `P3_PREP`），窗口收尾又靠
replay/REBUILD 的竞态决定停在哪个档；T3 成功也会被 `app_guide_window_close()` 拆环重放 23。
本设计要消掉这三处偏差。

## 2. 现状与偏差（代码锚点）

| # | 位置 | 现状 |
|---|---|---|
| D1 | `components/calibration_manager/inc/guide_action_plan.h:40` | 只有单个 `profile_level`，同时被当作“P2/P3 的环档”和“T3 的准入/目标档”；P1 分支不读它（`application/src/app.c:2300-2333`） |
| D2 | `components/calibration_manager/src/calibration_manager.c:4128-4134`、`:4154` | coded 测量载波取 `staged.baud_hz`（环状态的副作用），所以必须先切环档 |
| D3 | `application/src/app.c:3530-3537` | `P3_PREP` 的 `stage/apply(profile_level)` 对 P3 测量并非必需（`components/tdma/src/tdma_pio_spi_phys_p3.c:74-80` 用 `clock_get_hz(clk_sys)` + 请求 `baud_hz` 自算分频），只留下“staged 变 7”的副作用 |
| D4 | `application/src/app.c:2500-2505`、`:3540`、`:2739-2742`、`:1755` | R 段 trial 起点逐板锚在“收到 trigger 的时刻 + 6000 + 80×period”，10 kHz 下每板带一份逐跳交付偏差；唯一补偿是固定 300 ms |
| D5 | `application/src/app.c:108-124`、`:812-825`、`:3883-3928` | 窗口收尾档由 replay（23）与 REBUILD（继承 active 档）竞态决定，无显式声明 |
| D6 | `application/src/app.c:3026-3037` → `:2174-2206` | T3 成功也走 window close → replay 23，10 MHz 不保留 |

## 3. 设计 A：计划字段拆分（对应 D1）

- 在 `guide_action_plan_t` 里把 `profile_level` 拆成 **`guide_level`**（窗口/环的引导档，恒 23）与
  **`measure_level`**（R 段的测量目标档，P2/P3/T0–T2 = 7；P1 = 0 表示不切）。
- C 段的编码保持向后兼容：现有 `profile_level` 字段继续承载 **`measure_level`**；新增 `guide_level`
  放到 C 段未用的保留槽（或由 boot profile 推导，不占 wire 字段）。**wire 变更需冻结契约并登记。**
- `plan_valid` 增加：`measure_level ∈ {0,7,23}`；P1 要求 `measure_level == 0`。
- 迁移：旧 host 工具下发的计划 `guide_level` 缺省 = boot profile 档，行为与现在一致。

**判据**：`guide_action_plan` host 用例覆盖两个字段（含旧编码解析）；P1 的 `verify` 中
`level` 显示 0/23 而非 7。

## 4. 设计 B：R 段共享 epoch（对应 D4，真正的间歇失败修复）

**问题**：trigger 是逐跳转发的，各板收到时刻差最多 ~1 rev（10 kHz 下 600 ms）；`t0` 逐板自锚，
固定 300 ms guard 只覆盖一个方向。

**设计**：把“R 段起点”改成**按环帧序号对齐**：

1. trigger（或一个专用 barrier 帧）携带**参考板的主帧序号 `S0`**（`epoch` 字段复用）；
2. 各板在收到后，用自己的 **`down_rx_sequence`**（ring 快照已有，跟随的是主帧序号）等到
   `down_rx_sequence == S0 + K`（K 覆盖“停环 + apply 测量档 + arm”的耗时，建议 K = 2–3 rev）；
3. 到点后统一执行“停环 → arm 自己的角色（initiator/responder）→ 突发”。

这样所有节点在**同一个主帧周期**进入 R 段，误差 ≤ 1 个环周期（而不是“逐跳交付差”）。

**前置**：
- 停环必须在 barrier 之后（barrier 帧要送达所有节点）；
- `down_rx_sequence` 的可见性与“每 rev 一帧”的节奏需实测确认（`RING:STATus?` 已输出该字段）；
- 若边沿对齐仍不够，再考虑“响应对 initiator 的突发做一次 ack 重试”。

**判据**：同一 build、10 kHz 入口连续 ≥3 轮 P3 四板 `0xFFF`；失败位不再随 `start_link` 迁移；
四板 `t0` 差 ≤ 1 rev（需把 `t0`/`barrier_seq` 加进 `PLAN?` 只读字段）。

**回退**：若 `down_rx_sequence` 对齐不可用，退回“扩大 guard + 逐 trial 重试”，并明确标注为
缓解而非修复。

## 5. 设计 C：T3 后 10 MHz 常驻（对应 D5/D6）

- 窗口收尾**显式化**：`REBUILD` 前 `stage+apply(guide_level)`；`replay` 与 `REBUILD` 二者收尾档一致；
- T3 的 S 段成功后：`stage/apply(7)` + 重成环，并**置一个 latch**（例如 boot profile 的
  `profile_source` 提到“已标定运行档”，或 `BOOT:PLAN?` 增一个 `run_profile` 字段），使后续窗口
  关闭不再 replay 回 23；
- 判据：T3 进 Q 后四板 `baud_hz=10000000` 且 `BOOT:PLAN?` 仍 `boot_done=1/fail_step=0`。

**风险**：持久 calibration stage 的 `profile_crc32` 若属于 7 档，收尾改回 23 可能撞 ARM 的校准门
（`components/distributed_refmem/src/distributed_refmem_status.c:1227-1240`）；需要 no-cal 路径覆盖。

## 6. 实施顺序与判据

1. 设计 A（字段拆分）+ host 用例 → 判据：host 全绿；
2. 设计 C 的“收尾确定化”+ P3 删除多余 apply + P2 显式 baud → 判据：任一动作收尾后四板 `baud=10000`；
3. 设计 B（共享 epoch）→ 判据：10 kHz 入口 P3 连续 3 轮四板 `0xFFF`；
4. 设计 C 的 T3 latch → 判据：T3 后四板 `baud=10000000`；
5. 以 11-07 基线重跑（`20261107-211534-action7-gen2410`）→ 判据：`ladder=6/2(DONE)` + T3 后 10 MHz。

每步都要 P3 凭证；文档状态随实现更新。

## 7. 未决问题

1. `down_rx_sequence` 是否严格每 rev 递增一次（决定设计 B 的对齐精度）；
2. C 段是否还有可用保留槽（决定设计 A 是否要动 wire 契约）；
3. T3 latch 用 boot profile 还是新字段（影响 `BOOT:PLAN?` 的兼容性）；
4. 持久 calibration stage 的 `profile_crc32` 归属，决定收尾改 23 是否撞校准门。
