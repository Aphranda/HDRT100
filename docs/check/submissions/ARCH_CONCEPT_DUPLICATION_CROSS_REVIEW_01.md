# 核验提交单：重复概念合并/归档计划（ARCH）→ HAOFV 顶层

Status: Active
Domain: Architecture
Canonical: `docs/check/submissions/ARCH_CONCEPT_DUPLICATION_CROSS_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-11-05

## 提交内容

两份只读审计（运行时/协议、持久化/表）汇总出的"同一路径写了两份"清单与合并/归档计划：
`out/audit/CONCEPT_DUPLICATION_20261105.md` §11（A1–A11 合并、B1 归档、C 保持）。
同时提交三处文档标注（`73a15a3c`）：`refmem_action_registry` 已被统一控制面取代、
`tdma_seat_admission` 未接线、`REFMEM-ACTION-01` 登记为 superseded。

## 独立交叉评审（C11）

- 审核方：只读子代理 `ac0e9f7d`（未参与审计与计划编写）
- 基线：HEAD `73a15a3c` + 9 个并行会话 staged 文件
- 结论：**计划需修订（REVISE）**；A 项 4 处、C 项 2 处、批次划分需重排
- 方法：读源码/git/只读命令；P3 指纹与链接器 GC 声明未做构建验证，标为未验证

### 评审发现与处置

| 项 | 评审结论 | 证据 | 处置 |
|---|---|---|---|
| A1 | 重复成立，但"行为不变"不成立 | 3 个 store 流程逐语句相同；seat 缺写后校验；commit offset `36` 硬编码 3 份（`tdma_boot_profile_store.c:18`、`calibration_training_store.c:12`、`tdma_seat_mount_store.c:12`，owner 在 `flash_store_record.c:17`）；v1 map 下 boot store 编译为 stub | 拆出"导出 commit offset"；补校验是新增失败路径/reject code，需与 A8 同批；P3 只覆盖 2/3 store |
| A2 | 请求字面量 9 处（非 7），且含 async/lease 变体 | `portable_ota_core_port.c:243/404/423`、`ota_metadata_flash_app.c:72/131` 等；`_ao_submit` + 持久 lease | helper 需带 lease/context 与 execute/submit 两种形态 |
| A3/A5/A7 | 确认可合并 | `vdc_domain.c:315-318` ≡ `sync_io_model_sched.c:765-768`；zero-field CRC 逐字副本 | 保持 |
| A4 | 确认，且还有 3 个 LE reader 家族 | `refmem_le.h`、`refmem_model_read_u32_le`、`refmem_table_read_u32_le` | 扩大 A4 范围 |
| A6 | 重复成立，但复用会让 staging 更严格 | 模型 parser 校验 version/count（`:566,:595,:625`），registry 直接返回 true（`:232,:284`） | 明确"拒绝相位前移"，不写成"低风险删 200 行" |
| A8 | 只有 3 份近似（非 5） | action/counter/gateway；preview 是 `ring_stopped()`、deployment 是 `deployment_idle()`；counter 少 table-CRC 检查 | 只合并 3 份；冻结 reject code 41-50/51-58/61-65 |
| A9 | 重复成立，但溢出语义不同 | `vdc_timestamp_clock.c:106-123` 显式回绕；`:162-179` 失败；`trigger_time_cursor.c:160-172` 失败并回退 | 只合并"可整除快速路径"，不统一溢出策略 |
| A10 | **不合并** | journal 三段分别是 replay / latest / identity-filtered，是掉电完成证据 | 从计划移除，保留 |
| A11 | 确认，但改的是生成后的 `.c` | `refmem_command_catalog.c` 与 `refmem_function_catalog.c` 均已编译 | 不计 host 批次，需 P3 |
| B1 | 确认可归档，但有前置条件 | 生产零调用；`refmem_domain_config` + catalog 已覆盖分发；`registry_crc` 无消费者；`action_id` 非现行路径 | 已 supersede `REFMEM-ACTION-01`；归档需同批移除 host test/tool 注册并单独 P3 |
| C1/C2 | 保持结论对，理由错 | position_ack 满时丢**最新**；storage 无 drop-oldest，event_bus 满时拒收 | 修正理由，不改结论 |
| C3/C7 | 未验证 | 四个 session/epoch 机未逐行比对；`vdc_reference_config` handler 已注册 | 标 HYPOTHESIS，不作合并依据 |
| M1/M2/M4 | 计划遗漏 | 序列比较手写 ≥10 处；commit offset 3 份；LE reader 3 家族 | 补入计划（M1 新 A 项、M2 并入 A1、M4 并入 A4） |
| 基线 | 计划写 11 个 staged 文件，实际 9 个；`refmem_command_catalog` 已提交 | `db84f0d4` | 更正计划基线 |

### 批次重排（评审建议，采纳）

1. **批 1**：A2 / A3 / A4（含 M4）/ A7 —— 机械、零布局影响。
2. **批 2**：A1 + A8 同批（共用 seat_mount reject 通道，分开会留下 reject code 迁移中间态）+ A6。
3. **A9 单独**：需要 core1 计时证据。
4. **A10 不合并**；**C4 product_config 不碰**。
5. **B1 归档**：先 supersede 契约，再改文档/host/CMake，单独 P3。

## 偏差声明

- 计划原批次"A1–A7 一批"被评审判定过宽：混合零风险、机械与行为收紧项，且共用一次 P3。已按上表重排。
- 计划原文"A10 低风险"被撤回：journal 是掉电证据，不合并。
- 计划原文"A11 host 车道"被撤回：生成物是编译源，改它需 P3。
- `REFMEM-ACTION-01` 由 `pending` 改为 `superseded`（不删除，符合 AGENTS 规则 4）。

## Alternatives considered

- 方案A 按原计划 A1–A7 一次合并：拒绝，风险混合、P3 结论不可归因。
- 方案B 全部只文档化不合并：拒绝，A3/A5 等零风险项没有理由继续重复。
- 方案C 立即删除 action registry：拒绝，仓库规则要求归档/留档并 supersede 契约。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION（按评审重排后执行）
- 核验人: 计划编写会话

## 交叉审核记录（C11，必填）

- 审核方: 只读子代理 `ac0e9f7d`（独立会话，未参与编写）
- 审核方式: agent 交叉（读源码/git/只读命令；无写操作、无构建、无台架）
- 审核结论: PASS_WITH_CHANGES（4 处 A 项、2 处 C 项、批次重排、契约 supersede 前置）
- 审核日期: 2026-11-05
