# 核验提交单：TDMA resident process image -> HAOFV / 状态机

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_CROSS_REVIEW_04.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-09-02

## 提交内容

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| TDMA 是确定性 transport 唯一 owner | 符合 | resident image 的初始化、cycle boundary、segment owner、退出条件和 evidence 均由 TDMA owner 管理。 |
| 飞行处理服务持续流中的循环内存 | 部分符合 | `tdma_flight_engine_apply_impl()` 已先保留 incoming，再只覆盖本节点授权 segment；当前 adapter 尚未完成一次注入后的常驻 cycle。 |
| 单轮多 Node LOAD/UNLOAD | 部分符合 | `TdmaProcessImageMap` 和局部 overlay 已有基础；四板同一轮多 owner generation 的在线替换和原始波形证据尚未完成。 |
| frame completion 不终止 RUNNING | 偏差 | `tdma_adapter_comm_fsm` 仍有 `TDMA_ADAPTER_COMM_STATE_FRAME_COMPLETE`，完成后需要再次 ARM，与 resident cycle 目标不一致。 |
| 无更新时保持循环值 | 符合基础 | flight engine 在无可用新 TX image 时保留输入字节并记录状态；仍需 resident ring HIL 证明长期透传。 |
| STOP、复位、故障或重新配置受控退出 | 部分符合 | persona/resource lifecycle 已有 STOP/RESET/FAULT 基础；重新配置前 quiesce 和 resident image 再初始化仍待统一 FSM 验证。 |

## 偏差声明

- `TDMA-RESIDENT-01` 保持 `pending`。目标语义已冻结，但
  `tdma_pio_spi_ring_adapter_tx_beacon()` 仍按周期构建发送内容，
  `tdma_adapter_comm_fsm` 仍以 `FRAME_COMPLETE` 作为一轮终点。
- `hop_limit` 继续用于约束单个物理 frame instance 的拓扑传播；它不能成为 resident
  process image 的正常退出条件。返回 origin 后应执行本地 UNLOAD/LOAD 并进入下一 cycle。
- 当前文档更新不代表固件、PIO、构建、OTA 或 HIL 已完成。后续实现变更必须通过 TDMA
  短帧闭环，并用 NO1--NO4 的 SD 原始波形证明持续循环和单轮多 Node overlay。

## Alternatives considered

- 每个 Node 单独发起一帧，按环序完成多轮交换（拒绝：所有 Node 更新需要累计多个 cycle）。
- origin 每圈从空白 payload 重建新 frame（拒绝：破坏循环内存和无更新持续透传语义）。
- 一次注入 resident image，各 Node 在固定 segment 就地 UNLOAD/LOAD，origin boundary 继续下一 cycle（接受）。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: TDMA canonical / 状态机 canonical / 当前 C 接口层间核验

## 交叉审核记录（C11，必填）

- 审核方: `HAOFV_ARCHITECTURE.md`、TDMA/状态机三件套与 flight engine、ring adapter、adapter FSM 代码锚点
- 审核方式: 文档交叉 + 层间交叉
- 审核结论: PASS_WITH_NOTE（目标语义一致；实现偏差已进入 `TDMA-FLIGHT-001` 和 `SM-RES-010`）
- 审核日期: 2026-09-02
