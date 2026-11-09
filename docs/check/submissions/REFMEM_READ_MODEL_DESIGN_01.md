# 校验提交单：REFMEM 读模型扩展（DataLink 复用 vs RefMemSlotContract）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_READ_MODEL_DESIGN_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `components/distributed_refmem/inc/refmem_application_model.h`, `components/distributed_refmem/inc/refmem_sync.h`
Last updated: 2026-11-09

## 1. 问题

`SYSTem:REFMEM:OBS?` 已能返回逐槽行（M4），但对端行永远 `valid=0`。发布面（R1-3）按当前码无法直接落地：

- `DataLinkTable` 14 条已满，条目是**标量** `u32`（`refmem_application_model.h:424-446`），无 schema/宽度；
- `REFMEM_DELTA` 接收端拒绝 `field_width > 4`，且每个源槽只有一个 mirror
  （`refmem_sync.c:82-96`、`refmem_sync.h:191,209`）；
- 8 个读 key 共 **32 个 schema word**（`config/refmem/read_catalog.yaml`），一条 DataLink 只能承载一个 u32。

因此“只补 runtime”不够；需要一次模型/镜像层的选择。

## 2. 方案对比

| 方案 | 做法 | 影响 | 判定 |
|---|---|---|---|
| A | DataLink 14→46（每个 word 一条） | `REFTMEM_APP_MODEL_VERSION`/wire size/table CRC/package CRC 全变；`writer_instance` 需落在 12 个实例内 | 可行但表膨胀、包/版本升级面大 |
| B | 给 DataLink 条目加 `width/words` 数组语义 | 改 wire schema、版本、CRC、解析器；旧包 fail-closed | 改动最大 |
| **C（推荐）** | 实现架构已定义的 **`RefMemSlotContract` 静态派生视图**承载 OBS 字段（`field_slot_id` 由契约分配，如 `0x100+key*16+word`），扩展 DELTA 接收端为 `(slot, field_slot_id)` 镜像 | DataLinkTable/模型 version/CRC/package **不变**；需扩 mirror 与发布器；需 TDMA 确认 field/value 每周期通路 | 与架构文本一致，改动面最小 |

C 的关键：`field_slot_id` 不是“私有裸命名空间”，而是 `RefMemSlotContract` 分配的契约 id，带
`type/value range/writer/timestamp/stale/sync_policy`（架构 §「字段级 RefMemSlotContract」）。
这修正了上一版 `0x100+...` 绕过契约的错误。

## 3. 推荐方案 C 的组成

1. **契约视图**：新增静态 `refmem_obs_contract[]`（8 key × 各 word），字段
   `field_slot_id/type/writer_instance/permission/update_period_us/stale_window_us/sync_policy=delta`；
2. **镜像**：`refmem_sync` 增加 `[slot][field_slot_id]` 数组（约 2–4 KB SRAM），
   保留既有单值 mirror 兼容现有字段；
3. **发布器**：core0 服务按 `update_period_us` 采样，把 `(field_slot_id, value)` 写入
   process-image 的 REFMEM field/value 片（现有 `TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET/VALUE_OFFSET`），
   每周期一个 word，32 word 一轮；
4. **读侧**：`OBS?` 对端行读 mirror，`seq` = 该 key 完整一轮的发布序号，`stale` = 一轮超
   `stale_window_us`；
5. **单写者**：每 key 的 writer 必须唯一，沿用各域 owner（SYNC_IO/GUIDE/BOOT/TDMA/CAL/TRIGGER）。

## 4. 影响面

- **模型/包**：DataLinkTable 不动 → `REFTMEM_APP_MODEL_VERSION`、wire size、table CRC、
  package CRC 均不变；
- **TDMA**：复用现有 process-image REFMEM 10 B 片的 field/value，不新增帧类型/席位；
  需确认该片每周期确实被发送（而非仅 probe 路径）；
- **内存**：mirror 2–4 KB SRAM，需核算 RAM_TEXT/静态预算；
- **契约**：`REFMEM-READ-01` 仍 pending，扩展视为同一契约的实现细化，不新增契约 id。

## 5. 验收判据

- host：契约表 schema/宽度的编译期断言；mirror 的 `(slot,field)` 读写、轮转 seq、stale 用例；
- 单板：`OBS? <key>,0,4` 本地行 `seq` 随发布推进，停发布后 `stale=1`；
- 四板：每 key 4 行、`stale=0`；拔一板 → 该行 `stale=1`；
- 不新增帧类型/席位/模型版本（`git diff` 可证）。

## 6. 待 C11/TDMA 决策项

1. process-image REFMEM field/value 是否可作通用发布通路（现仅 action probe 使用）；
   代码显示该片每周期**会**被组帧发送（`distributed_refmem.c:889-897`），但同一 field/value
   与 position-ack/diagnostic 共享并已做仲裁，OBS 发布必须排队、不得抢占 first-class fact；
2. `field_slot_id` 范围（`0x100+`）与 `u16` 空间分配；
3. mirror 放 `statistics` 区还是静态 SRAM；
4. `writer_instance` 映射（现有 12 实例是否覆盖 8 key 的 writer）；
5. `stale_window_us` 取值与发布周期预算。

## 7. 结论

**待独立 C11/TDMA 复核。** 本文件不冒充通过；推荐 C，但“process-image 每周期 field/value”
这一前提需 TDMA 侧确认，否则退回 A（表扩展）并接受版本/包升级成本。
