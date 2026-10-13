# 核验提交单：TRIGGER（T2 位置切片） → RefMem/TDMA 层

Status: Active
Domain: TRIGGER
Canonical: `docs/check/submissions/TRIGGER_CROSS_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-13

> 提交方: T2 域（作者） → 核验方: **独立 AI 评审**（独立 subagent `2805f687-5ff5-42ef-851e-73d9d61f43bc`）；
> 方式: 代码与文档**只读交叉核验**（C11）；核验未创建/修改/删除任何文件，未运行构建或硬件工具。
> 结论: **APPROVE WITH NOTES**（无编造项；三项为措辞/归属缺陷，已按下述**修正登记行**）。

## 提交内容

| 父层/对端条款 | 符合性 | 证据 |
|---|---|---|
| ① wire 尺寸与布局（10 B 小端、tag 0xE0/0xF0、字段偏移、id/version/fine 约束） | ✅ | `components/sync_trigger/inc/trigger_position_slice.h:13,14,15,16,38-41,44-58,60-75` |
| ② 尺寸静态断言（等于 TDMA process image RefMem 段） | ✅ | `components/distributed_refmem/src/distributed_refmem.c:33-34`；常量值 **10u** 见 `components/tdma/inc/tdma_process_image_layout.h:72` |
| ③ 容器：8×32 B process image；前 8 B core1 元数据；后 24 B core0 内容 | ✅（文档原文） | `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:136-142` |
| ③′ 记录的**实际容器**是 10 B RefMem 子区（mailbox 偏移 14–23） | ✅ | `docs/tdma/TDMA_SEAT_TX_CHANNEL_REVIEW.md:65`；`docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md:104` |
| ④ owner = `TriggerRegion[n]`（当前 Owner 表） | ✅ | `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:1105`（表起 :1092）；注意 :1022 的区偏移行位于**历史快照**节 :1006 |
| ⑤ 每帧一条记录、PREVIEW/FREEZE 不拆分 | ✅ | `components/distributed_refmem/src/distributed_refmem.c:1046-1051`（注释 :1047 逐字） |
| ⑥ ack 路径校验**帧级** CRC；"无**独立** RefMem CRC" | ⚠️ 部分（措辞已收紧） | ack 接收校验帧级 CRC：`distributed_refmem_position_ack.inc:165-166`（`tdma_process_image_crc16_ccitt`，覆盖 0..29）；逐字节比较/暂存为 **priority-lane RX 去重缓存**：`:205-207,286`；ack 控制字 packing/比较：`:154-155,167-168,88,109` |
| ⑦ 布局版本耦合归属 **TDMA process-image**（非 REFMEM-LAYOUT-01） | ⚠️ 部分（已改归属） | `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:996`（提升规则）与 **:1001**（"TDMA wire 独立版本不随之改变"）；注册契约 `TDMA-PROCESSIMAGE-01` |
| ⑧ clause_loc 文档须 Active/Frozen | ✅ | `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md:3` `Status: Active`；门禁实现 `tools/doc_regression_check.py:257` |

## 偏差声明

核验方指出三处**措辞/归属缺陷**（均已修正，无实现缺失、无编造）：

1. 原行"容器为 TDMA mailbox RefMem 的 **24 B 内容段**"**与自身 `== TDMA_PROCESS_IMAGE_REFMEM_SIZE`（=10 B）条款矛盾**；
   → 已改为："容器为 **10 B 的 RefMem 子区**（offset 14、size 10），位于 mailbox 的 24 B core0 内容体之内"。
2. 原行"随 **REPMEM-LAYOUT-01** 的布局版本匹配"**归属错误**（该节 :1001 明确 TDMA wire 版本独立）；
   → 已改为："尺寸随 **TDMA process image 布局版本**耦合（`TDMA-PROCESSIMAGE-01`）"。
3. 原行"ack **逐字节比较**"**过宽**（逐字节属 priority-lane RX 去重缓存；ack 接收路径校验帧级 CRC）；
   → 已改为："CRC 由 **TDMA 帧级**承担，RefMem 层不做**独立** CRC；ack 接收路径校验该帧级 CRC"。

**附带记录（供读者防混淆）**：`0xE0` 既是 PREVIEW tag 的高位取值，也命名 TDMA 的 T2-frozen 控制操作码
（`components/tdma/inc/tdma_process_image_layout.h:105`），但二者位于**不同字节/字段**；且因首字节为 `tag|position_id`，
字面 `0xE0`/`0xF0` 本身不是合法记录（实际首字节为 `0xE1..0xEF` / `0xF1..0xFF`）。

**登记状态**：`ARCH-TRIGGERSLICE-01` 保持 **`pending`**（实现与四板验证完成前不得置 `active`，与 VDC/ARCH_FLASH 提交单先例一致）。
