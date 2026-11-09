# 校验提交单：REFMEM 读模型扩展（DataLink 扩表 vs RefMemSlotContract）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_READ_MODEL_DESIGN_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `components/distributed_refmem/inc/refmem_application_model.h`, `components/distributed_refmem/inc/refmem_sync.h`, `components/distributed_refmem/inc/refmem_vector_table.h`
Last updated: 2026-11-09

## 1. 问题

`SYSTem:REFMEM:OBS?` 已能返回逐槽行（M4），但对端行永远 `valid=0`。发布面（R1-3）按当前码无法直接落地：

- `DataLinkTable` 14 条已满，条目是**标量** `u32`（`refmem_application_model.h:424-446`），无 schema/宽度/offset；
- `REFMEM_DELTA` 接收端拒绝 `field_width > 4`，且每个源槽只有一个 mirror
  （`refmem_sync.c:80-102`、`refmem_sync.h:191,209`）；
- 8 个读 key 共 **32 个 schema word**（`config/refmem/read_catalog.yaml`），一条 DataLink 只能承载一个 u32。

## 2. 运输事实核验（2026-11-09，修正上一版错误）

上一版把 `distributed_refmem.c:889-897` 当作“每周期通用 field/value 通道”，**不成立**：

- 该处 `action_patch_mailbox()` 是 action probe / position-ack 的**仲裁**，不是通用发布口；
- 实际写入点是 `distributed_refmem.c:820-826`：`field_id` 恒写
  `TDMA_PROCESS_IMAGE_REFMEM_BASELINE_FIELD_ID`(0)、value 是 `s_service_count`
  （`distributed_refmem_plane.c:368-377,418-424`）；
- 10 B 片布局固定为 generation(4)+field_id(2)+value(4)，且与 position-ack/diagnostic 共享。

结论：**当前没有可用的“通用每周期 field/value”通路**；任何方案都必须先决定“选择器 + 仲裁”，
这是 TDMA 级变更，不能只补一个 runtime。

## 3. 方案对比（修正版）

| 方案 | 做法 | 影响 | 判定 |
|---|---|---|---|
| A | DataLink 14→46（每 word 一条），version 1→2 | wire size/table CRC/package CRC 全变；host packer 硬编码 14 与 LAYOUT_VERSION=2（`tools/refmem/refmem_table_image.py:28-38`）与 golden 同步；DataLink 无 offset 字段，仍需为 32 word 指定 region/offset 归宿 | 可行，升级面大 |
| **B（推荐，条件）** | 实现真实 `RefMemSlotContract` 静态表（`field_slot_id/offset/width/writer/type/update_period_us/stale_window_us`），mirror 改 `(slot, field_slot_id)`，发布器采样；在 10 B 片加**选择器 + 仲裁** | 不改 model version/CRC/package；需新表 + TDMA 选择器/仲裁 + region/offset 分配 | 最小模型改动，但依赖 TDMA 同意 |
| C | 维护/二等席新帧 | 维护流约 4 B/s，32 word 一轮约 32 s | 不能作为读模型 |

**关于 `0x100+key*16+word`**：不得再作为“推荐公式”。若 B 使用该范围，必须
(a) 由真实契约表分配 `field_slot_id`（不是裸魔数），(b) 同步修订
`REFMEM_DOMAIN_ARCHITECTURE.md:1505-1506` 中“该公式=重复造轮子、不采用”的冻结表述，
否则实现会与契约冲突。该项由 C11 先裁决。

## 4. 影响面

- **模型/包**：A 需 version 1→2、host packer/golden、region/offset；B 不动模型 version/CRC/package；
- **TDMA**：B 需在 10 B 片增加 field 选择器并保持 position-ack/first-class fact 优先；
- **内存**：mirror 2–4 KB SRAM（B）或表扩张（A），需核算；
- **契约**：`REFMEM-READ-01` 仍 pending；本扩展是其实现细化，不新增契约 id。

## 5. 验收判据

- host：契约表/镜像的编译期断言、`(slot,field)` 读写、轮转 seq、stale 用例；
- 单板：`OBS? <key>,0,4` 本地行 `seq` 随发布推进，停发布后 `stale=1`；
- 四板：每 key 4 行、`stale=0`；拔一板 → 该行 `stale=1`；
- 不新增帧类型/席位；A 需给出 version/CRC/package 变更证据。

## 6. 待 C11/TDMA 决策项

1. 10 B 片是否允许增加 field 选择器 + OBS 仲裁（B 的前提）；
2. 32 word 的 region/offset 归宿（15 个 region 已定长，TLV 被 param pool 占用）；
3. A 的 version 1→2 与 host packer/golden 升级窗口；
4. `field_slot_id` 范围与 `RefMemSlotContract` 表的所有权；
5. `writer_instance` 映射与 `stale_window_us` 取值。

## 7. 结论

**待独立 C11/TDMA 复核。** 推荐 B（条件：TDMA 同意选择器/仲裁），否则退回 A 并接受
version/CRC/package 升级成本。上一版的“0x100 公式 + 每周期通用 field/value”均已被证伪，
不得作为实现依据。

## 8. 复核记录（只读子代理 `33b11994`，2026-11-09）

- 确认 DataLink 14/14、标量、DELTA width≤4/单 mirror；
- 证伪“每周期通用 field/value”前提（实际写 baseline field 0 + `s_service_count`）；
- 指出 `0x100` 与架构冻结条款冲突、`RefMemSlotContract` 代码不存在；
- 结论：只补 runtime 不够；A/B 都必须含模型或契约表扩展，且 host packer/golden 同步。
