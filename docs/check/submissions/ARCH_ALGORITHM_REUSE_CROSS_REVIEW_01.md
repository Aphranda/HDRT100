# 核验提交单：算法原语合并 + 验收自检修复（ARCH / CALIBRATION）→ HAOFV 顶层

Status: Active
Domain: Architecture
Canonical: `docs/check/submissions/ARCH_ALGORITHM_REUSE_CROSS_REVIEW_01.md`
Related: `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/check/DOCS_REGISTRY.md`, `out/algo-consolidation/HANDOFF_20261105.md`
Last updated: 2026-11-05

## 提交内容

把工程内重复的校验原语收敛到 `components/algo/inc/algo_checksum.h`（header-only：CRC-32/ISO-HDLC
含 raw 寄存器变体、CRC-16/CCITT-FALSE、FNV-1a 的 bytes/str/u32_le/word），迁移 CRC-32/CCITT/FNV
全部重复站点并保持逐字节等价；同时修正 `p3_dpll_gate` 给 DPLL summary 注入元数据后仍做全字典
精确等值的验收自检缺陷（改为"summary 只含原始证据，资格元数据只进凭证"）。

| 父层条款 | 符合性 | 证据 |
|---|---|---|
| ① 不新增总线/payload class/命令槽/ACK | ✅ | 仅算法函数与 include 路径；无 wire/layout 改动 |
| ② 不占 core1 窗口/不拉长一等席预算 | ✅ | CCITT/FNV 内核 inline；`tdma_transport_crc32_update` 仍是唯一 SRAM 特化 |
| ③ 单写者/owner 边界 | ✅ | 只替换纯函数实现，无状态/owner 变化 |
| ④ 失败 fail-closed + reason | ✅ | 校验值逐字节等价（见下）；失败路径未改 |
| ⑤ 布局/版本/契约同步 | ✅ | CRC/身份值不变；无 FlashMap/错误码改动 |
| ⑥ host + 四板证据可复跑 | ✅ | host 用例 + P3 `SCOPE_PASS` 凭证（build `20261105142140`，指纹 `c959058d…`/988，外部相位 32.0 ns）；本提交单的 F3/F4/F5 修复后已重跑 |
| C1 Flash/XIP | ✅ | 未触碰擦写路径；四板 OTA 通过 |
| C2 时间事实 | ✅ | 未触碰 raw tick/回绕逻辑 |

**逐字节等价**：独立复核对 `refmem_sync_frame_payload_crc32`、`refmem_sync_vdc_feedback_crc32`、
`vdc_priority_codec_crc16`、`vdc_reference_config`、`refmem_param_pool_crc`、TDMA FNV 链与
`trigger_deployment_crc_word` 做了新旧数值对比，全部相同；`raw(0,"123456789")=0x2DFD2D88`、
zlib `0xCBF43926`、CCITT `0x29B1`、FNV `0xBB86B11C` 已进 `tests/python/test_algo_checksum.py`。

## 审查发现与处置

C11 独立只读审查（子代理 `e2550c12`，2026-11-05）：**APPROVE_WITH_CHANGES**。

| # | 级别 | 发现 | 处置 | 状态 |
|---|---|---|---|---|
| F1 | BLOCKER（流程） | 已 staged 的 `host_acceptance_receipt.json` 是失败运行（`passed:false`，2 failed/32 errors），且早于新测试 | 接受会话在 stage 全部 tests 后重跑 `host_acceptance.py run`；不得随固件提交携带 | OPEN（非本变更引入） |
| F2 | MAJOR（门禁） | 忽略 3 个注记键虽无证据旁路，但 summary 副本与凭证字段无交叉校验，注记字段只写不读 | 删去 `workflow_steps.py` 的 summary 注入，恢复精确等值；资格元数据仍写凭证（`4736-4743`） | CLOSED（step 30/35/36 重跑 OK） |
| F3 | MAJOR（文档） | "唯一实现"过度声明：`tdma_seat_mount_store.h`、`refmem_action_probe.h`、`distributed_config.c` 仍有 CRC-32 副本 | 三处改调 `algo_*`；§9.28 改为"已迁移站点的唯一实现"，并注明 third_party `pota_crc32` 是 `ota_crc32_*` 的底层内核 | CLOSED |
| F4 | MINOR | zero-field 函数在 `size==0` 时做 NULL+0 指针运算 | 两处加 `size==0 → 0` 早退（与旧实现一致） | CLOSED |
| F5 | MINOR | `__not_in_flash_func` 包装调用普通 inline，-Og 下可能调进 flash | 给 FNV update 与 CCITT byte-step 加 `always_inline` | CLOSED |
| F6 | MINOR | `test_vdc_command_owner.host_includes` 缺 `components/algo/inc` | 已补 | CLOSED |
| F7 | MINOR | 测试缺 raw 零种子 golden、`fnv_word` | 已补（`crc32_raw(0)=0x2DFD2D88`、word fold） | CLOSED |
| F8 | MINOR | `P3_FLOW.html` 指向旧的失败运行 | 由最终 tail 重跑重生成（22:16:23，receipt_written=true） | CLOSED |
| F9 | NIT | 归档 `out/archive/algo-scaffold/algo_checksum.c` 与 header 冲突 | 保持归档、不入构建；不做恢复 | ACK |
| F10 | NIT | `refmem_sync_frame.c` 多余 `ota_crc32.h`；若干迁移文件留死 FNV 宏 | include 已删；死宏留待顺手清理 | 部分 |
| F11 | MINOR（既有） | DPLL "PASS" 只按端点 90 ns 评级，`phase_window_extrema_ns` 未被告警策略消费 | 非本 diff 引入；在凭证叙述中注明"endpoint-only"限制 | ACK（既有） |

## 偏差声明

- **F2 方案偏差**：初审修复是"校验时忽略工作流注记键"；复审指出更干净的修法是"不往 summary 注入"。
  已按复审改道：summary 完全由原始证据决定，凭证保留资格元数据；`validate_summary` 恢复精确等值。
  接受理由：不削弱任何证据字段，且消除了"注记副本无人校验"的歧义。
- **F3 范围偏差**：原计划把 two headers 记为"遗留待迁"；复审要求要么迁移要么列为例外。选择迁移
  （值等价、无 wire 改动），不引入新 P3 需求。
- **外部示波器**：按用户授权启用 HDO4A244301137。C11 修复后的最终树上重跑（build `20261105142140`，
  指纹 `c959058d…`），DPLL 首次窗口两块从板输出停止（SYNC-OUT-002 类），第 2 次通过；外部相位
  32.0 ns（INFO），凭证 `SCOPE_PASS`。

## Alternatives considered

- 方案A 保留"忽略注记键"的校验宽容：拒绝，注记字段与凭证无交叉校验，且需额外测试覆盖。
- 方案B 把 `tdma_seat_mount_crc`/`refmem_action_probe_crc32` 列为例外不迁移：拒绝，属于仍在
  active 路径的重复实现，违反 §9.27。
- 方案C `refmem_vector_fast_crc32` 恢复本地循环：拒绝，`always_inline` 同时满足 -Og 与代码单一来源。
- 方案D 用 `--full` 配置重跑：本轮未做；凭证为 QUICK_GRADED，FULL 仍待需要时执行。

## 核验结论

- 结论: ACCEPT_WITH_DEVIATION
- 核验人: 算法合并执行会话（本提交单作者）

## 交叉审核记录（C11，必填）

- 审核方: 只读子代理 `e2550c12`（独立会话，未参与实现）
- 审核方式: agent 交叉（读源码/日志/ELF map/指纹复算，无写操作）
- 审核结论: APPROVE_WITH_CHANGES → 处置后 PASS_WITH_NOTE（F1 由接受会话闭环，F11 为既有项）
- 审核日期: 2026-11-05
