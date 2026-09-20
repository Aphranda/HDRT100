# Activation / Load 跨域主线架构

Status: Draft
Domain: ARCH
Canonical: `docs/arch/ARCH_ACTIVATION_LOAD_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/sync/SYNC_IO_ARCHITECTURE.md`, `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`
Last updated: 2026-09-05

本文档定义 HAOFV 下的 **Activation / Load 跨域主线**。它是一条装配主线，不是新的运行 owner：
它不拥有 RefMem、TDMA、Calibration、SYNC_IO 或 state_machine 的任何执行语义，只把这些域的
"staging → 校验 → gate → 激活 → 回滚"动作串成一条可对账、可追踪的跨域流水线。Activation/Load
主线的语义落点分散在各域 canonical；本文只收口"谁编排、谁门禁、谁执行、对账基线、升格条件"。

> 设计参照：与 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md` 的定位一致——T2 是
> "跨域装配主线、非 owner"，Activation/Load 同理。二者不同：T2 面向时间预约装配；
> Activation/Load 面向"多 profile / persona / 表 / 参数 切换"的一致性装配。

## 1. 主线定位

Activation/Load 主线回答的问题是：

```text
系统从一组 profile / persona / 表 / 参数 切换到另一组时，
各域如何在同一安全边界内完成 staging -> 校验 -> gate -> activate，
失败如何回滚并保留证据？
```

它不回答：

- VDC offset/rate 如何计算（属 VDC）。
- TDMA 帧格式与 payload（属 TDMA）。
- persona 程序内容（属各 owner 的 PIO program）。

主线不新增运行 owner，不替代各域 canonical，不引入"运行时动态部署"语义
（与顶层拒绝完整 IEC 61499 分布式运行时 / 动态部署 FB 一致）。

## 2. 跨域角色与 owner 边界

| 角色 | 现 owner（代码/组件落点） | 在主线中的职责 |
|---|---|---|
| 编排 | ConfigGate / SystemAO（`components/system_manager`） | 提交配置意图、发布 config ACK/NACK；协调跨域切换顺序。 |
| 门禁 | DeploymentGate（表落点在 `components/distributed_refmem`，语义求值现状见 §5） | RUN 前一致性的"一票否决"语义：build / hw / config / cal / sync / layout / 共存。 |
| staging 基础件 | RefMem table registry（`refmem_table_registry`，含 staging lease / activation gate） | 表镜像与 TDMA/Calibration 表状负载的 staging→activate 事务原语。 |
| 执行 | 各域 activation：RefMem `activate_staging`、TDMA profile apply/ARM、Calibration `activate_path_candidate`、SYNC_IO persona arm、state_machine program manager | 各自域内执行激活或装载，语义留在各域。 |
| 资源仲裁 | state_machine program manager + resource arbiter | persona 的 PIO/SM/DMA/GPIO claim/release 与回滚。 |

主线本身不拥有上述任何一项的 writer 权；本文只描述它们如何被串成一致切换。

## 3. 三类"加载"的区分（术语治理基线）

代码审计（2026-09，见 §5）确认代码中已是**三个不同机制**，不合并、但必须区分命名：

| 类别 | 代码动词 | 回滚粒度 | 例子 |
|---|---|---|---|
| 逻辑角色/实例部署 | stage / commit / activate | 跨板一致性 + realtime idle gate | RefMem NodeLoad / 表镜像激活 |
| PIO persona 程序安装 | claim / load / arm / start / stop / release | PIO/SM/DMA 生命周期 + 失败回滚 | SYNC_IO persona manager、TDMA program manager |
| 参数 / profile 激活 | stage / apply / activate | stopped-ring gate / candidate→active | TDMA operating profile、Calibration path snapshot |

命名规则：

- `load` 只用于 persona 程序安装语义；NodeLoad 表改称 instance deployment 表；
  与"读内存"无关的 `load` 命名需避免（如 runtime 读取不得叫 load）。
- `persona` 只用于可执行 PIO program persona（SYNC_IO / TDMA program persona）；
  RefMem NodeLoad 的 `persona_mask` 是逻辑角色字段，不得与 PIO persona 混称。

## 4. 标准流程（Activation Lifecycle）

跨域一致的激活流程骨架：

```text
STAGING     意图只写 staging（双缓冲 / lease），不触 active
  -> VALIDATE   CRC / schema / generation / owner 资格
  -> GATE       域 gate + DeploymentGate（资源兼容 / 一致性）
  -> ACTIVATE   原子切换：新 -> active，旧 -> rollbackable
  -> 失败回滚   fail-closed：保持旧 active，发布 evidence
```

域 gate 语义保留在各域：

- RefMem：activation gate 8 位（idle / flash / crc / owner / slot claim / deployment / ack…）。
- TDMA：profile apply 要求 ring stopped；ARM 校验 runtime config / calibration gate。
- Calibration：candidate→active 需 evidence gate + VDC 消费方接受。
- SYNC_IO / state_machine：persona claim→load→arm 失败回滚（cleanup / ROLLING_BACK）。

## 5. 代码对账基线（2026-09-05 只读审核快照）

以下为三份只读代码审核的结论快照（非代码事实源；事实以代码符号为准，位置引用可复核）：

| 主题 | 结论 | 证据方向 |
|---|---|---|
| DeploymentGate 落地 | 文档已定义、代码"部分接线"、非强制闭环：12 项表仅 QUALITY 有运行时求值，其余恒为镜像携带值；表级 getter 生产代码 0 消费 | `components/distributed_refmem`：`refmem_application_model`、`refmem_table_registry`、`refmem_quality` |
| ConfigGate 强制力 | ConfigGate 是"本地 config 有效 + claim + quality"的 ACK/NACK 发布器；无强制 RUN 消费者 | `components/system_manager` |
| 原语重复度 | 仅 RefMem registry 三态镜像与 Calibration path snapshot 高度同构（值得收敛候选）；TDMA flight FIFO / ProcessImageMap / sync_io persona / OTA 语义不同，保持独立 | `refmem_table_registry` vs `calibration_path_snapshot` |
| 确定性小收敛 | CRC32/FNV 计算在三处重复实现，可共享 | `calibration_path_snapshot.c`、`tdma_operating_profile.c`、pota/refmem |
| persona 收敛度 | SYNC_IO persona manager 结构性存在，WAVE/SCHED/LA 已接入，INPUT_CAPTURE/SMA 未接入；TDMA program manager 已收敛（SM-FSM-001 DONE 属实） | `components/sync_io`、`components/tdma` |

## 6. 当前缺口与接线待办（指向既有 TODO）

- DeploymentGate 语义求值器未接线（12 项 check 中 11 项无运行时求值器）；完整冲突 linter
  仅 init 执行，镜像激活只跑 contract 子集 —— 对应 `docs/refmem/REFMEM_DOMAIN_TODO.md`
  activation gate 项与 `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md` ProcessImageMap→DeploymentGate 待办。
- TDMA / VDC / Calibration / OTA 未经过 DeploymentGate —— 对应 `docs/tdma/`、`docs/vdc/VDC_DOMAIN_TODO.md` P0.4 等。
- 共享 CRC 工具、RefMem⇄Calibration 激活骨架收敛为候选工程项（需域 owner 同意 + P3 验收）。

## 7. 升格条件（何时从 arch 单篇升级为独立子域）

本主线当前以 arch 单篇承载（参照 T2）。当以下条件满足后，升级为独立维护文件夹
（如 `docs/activation/`，前缀白名单 + 三件套 + README）：

1. DeploymentGate 求值器至少接入 RefMem + 一个业务域（TDMA/Calibration），
   "一票否决"在 RUN 路径真实生效；
2. 至少两个域已共享激活原语或共享 CRC 工具，出现持续 TODO/Task Progress 量；
3. 主线出现需要独立追踪的跨域切换证据（多 profile/persona/OTA 切换的回归与门禁记录）。

## 8. 边界

- 本文不登记契约（评审/主线定位文档，契约登记以各域 canonical + `docs/check/DOCS_REGISTRY.md` 为准）。
- 语义变更先改对应域 canonical 并按文档自回归流程登记；本文只收敛主线视图。
