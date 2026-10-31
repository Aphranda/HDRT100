# 核验提交单：ARCH-OTASTREAM-02（OTA TDMA v2 架构）C11 交叉复核

Status: Active
Domain: HAOFV / OTA / Documentation Governance
Canonical: `docs/check/submissions/ARCH_OTA_CROSS_REVIEW_01.md`
Related: `docs/arch/ARCH_OTA_TDMA_STREAM_DESIGN.md`, `docs/check/DOCS_REGISTRY.md`, `docs/ota/OTA_V2STREAM_PLAYBOOK.md`
Last updated: 2026-10-31

## 提交内容

对 `docs/check/DOCS_REGISTRY.md` 新增的 `ARCH-OTASTREAM-02`（v2：TDMA 维护流只承载 OTA
数据、控制面归 REFMEM、目标侧复用既有 AB 流程、按 V2-D 分期退役第二套 stream 会话）做
C11 交叉复核。复核对象：登记条款、`clause_loc`（`docs/arch/ARCH_OTA_TDMA_STREAM_DESIGN.md`
§13）与 `code_anchor`。

## 复核方式与结论

- 方式：独立只读复核（不同于登记者的复核上下文，2026-10-31），未改动任何文件。
- 结论：**有条件通过**；`ARCH-OTASTREAM-02` 保持 `pending`，`ARCH-OTASTREAM-01` 保持
  `pending`，暂不 supersede。

### 复核发现（须在转 active 前闭环）

| 项 | 结论 | 处置 |
|---|---|---|
| 与 §13 一致性 | 方向一致，措辞偏强 | 条款限定为 **`0x4F` OTA 流**；「退役第二套会话」改为「不再作为主路径、按 V2-D 分期退役」；补过渡期保留 SCPI `SYST:OTA:STREAM:*` 数据入口与 §13.4 控制面退出条件；生命周期句移出契约正文 |
| 与 ARCH-OTASTREAM-01 冲突 | 未裁决 | 01 保持 pending；02 只取代其 TDMA 承载/控制语义子句，其余五类本地 transport 仍由 01 覆盖；`HAOFV_FLASH_ARCHITECTURE.md` §7.2/§7.4 待 02 转 active 前同步 |
| 与 ARCH-FLASHOWNER-01 / ARCH-BOOTCTRL-01 | 不冲突 | 写路径仍为 `ota_fb → portable_ota_port_core_* → flash_transaction_ao_execute`（core0 AO） |
| 可冻结性 | 主体合格，登记文本需修正 | 补 C11 落档（本文件）；修正 `docs/README.md` 索引口径与文档 §1「契约转正」非目标口径 |
| `code_anchor` 语义 | 需改 | 改为真实落点（`ota_fb.c` 或桥接模块）；`ota_tdma_binding.c` 仍含被分期退役的 stream 会话，不宜作唯一锚点 |
| ⑥ 证据 | 未闭环 | 需 durable 递增 + 覆盖当前源码指纹的 P3；既有 QUICK 凭证不覆盖 |

## 可写入登记表的复核结论

C11 复核（2026-10-31，见 `docs/check/submissions/ARCH_OTA_CROSS_REVIEW_01.md`）：
方向与 §13 一致，保留 `pending`；须把「维护流」限定为 `0x4F` OTA 流、把「退役第二套会话」
改为「不再作为主路径、按 V2-D 分期退役」并补过渡期 SCPI 数据入口与 §13.4 退出条件，
`code_anchor` 改指真实落点；在 durable 递增 + 覆盖当前指纹的 P3 齐全前不转 active，
`ARCH-OTASTREAM-01` 保持 `pending`、暂不 superseded。
