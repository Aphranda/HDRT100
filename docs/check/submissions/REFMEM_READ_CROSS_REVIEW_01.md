# 校验提交单：REFMEM 读面契约 REFMEM-READ-01（逐槽 OBS 读模型）

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_READ_CROSS_REVIEW_01.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/calibration/CALIBRATION_REFMEM_CONTROL_PLANE_MIGRATION_PLAN.md`, `tools/refmem/control_plane.py`
Last updated: 2026-11-09

## 1. 提交内容

- 契约 `REFMEM-READ-01`：只读本地镜像、逐槽拼装、不做同步跨板读（D0 决策 2026-11-09 选 A）；
- `config/refmem/read_catalog.yaml` 8 个 read key + 生成器与静态表；
- `SYSTem:REFMEM:OBS? <key>,<offset>,<count>` 查询；
- 6/8 key 的本地 provider（`switch1/2`、`guide_plan`、`boot_plan`、`ring_status`、`cal_status`）；
- `tools/refmem/control_plane.py` 的 `read/write/wait_terminal` 与 `tools/refmem/control_plane_smoke.py`。

## 2. 当前实现状态（2026-11-09，build `20261109001932`）

| 项 | 状态 | 证据 |
|---|---|---|
| 读目录 8 key | 已生成 | `refmem_read_catalog_table.c`、`test_refmem_read_catalog.c` |
| `OBS?` 接线 | 已实装 | `scpi_refmem_snapshot_commands.c:1325-1430` |
| key provider | 6/8 | `836dae7a`、`c6f2c76e`、`3624f322` |
| host 用例 | 通过 | `test_refmem_obs.c`、`test_refmem_control_plane.py`（8 passed） |
| 单板 smoke | 有归档 | `out/refmem_read/obs_smoke_20261109.txt`（build `20261109051835`：`OBS? 6,0,4` 四行、`OBS? 1,0,2` 两行） |
| 逐槽行 / 发布面 / 2 key / 四板 HIL | 逐槽行 **M4 已做**；其余未做 | 架构 §「未实装」 |

## 3. 审计发现与处置（2026-11-09）

| # | 发现 | 处置 | 状态 |
|---|---|---|---|
| 1 | 契约写逐槽行 `slot,valid,epoch,seq,age_ms,stale,<schema>`，实现只返回本板单行 `key,offset,count,valid,age_ms,<schema>` | M4 已实装逐槽行（本地 valid、对端 stale）；真实 `seq/age` 待 R1-3 | CLOSED（M4，seq/age 待 M2） |
| 2 | `ControlPlane.wait_terminal` 调不存在的 `SYST:REFMEM:COMMAND:STATus?` | 改读 `SYST:COMMAND:TARget? <slot>`，终态 4/5/6 | CLOSED（本轮） |
| 3 | `control_plane_smoke.py` 仍把 5/7 key 当未实装 | 改为 PILOT={1,2,4,5,6,7}、NOT={3,8} | CLOSED（本轮） |
| 4 | `SCPI_COMMANDS.md` 写「实现待 M2，当前返回 -200」 | 改为「pilot 6/8 已实装」并补逐槽行说明 | CLOSED（本轮） |
| 5 | 登记表 READ-01 证据写「pilot 4 key / 待 host / 待 smoke」 | 回写 6/8 + host/smoke 已完成 + 剩余项 | CLOSED（本轮） |
| 6 | 契约 id `REFTMEM-READ-01` 与登记 `REFMEM-READ-01` 混用 | READ-01 三处改为 `REFMEM-`；历史 `REFTMEM-LEASE/IDLE` 为既有登记，不批量改 | PARTIAL（READ-01 已一致） |
| 7 | `OBS?` 硬编码 `4u` 而非 `DISTRIBUTED_REFMEM_NODE_COUNT` | M4 改用 `DISTRIBUTED_REFMEM_NODE_COUNT` | CLOSED（M4） |

## 4. 独立复核记录（只读子代理 `b2e480b5-f191-4364-9318-66813ecff1e3`，2026-11-09）

基线：评审期间 HEAD `7c2ed8e4` → `fc0a1de0` → `31e4f15f` → `9d0763a0`；未改文件、未跑硬件。
5 组声称全部「成立」，另提 9 项新发现。

| # | 声称 | 结论 | 差异 |
|---|---|---|---|
| 1 | `ControlPlane.write/wait_terminal` 接真实入口 | 成立 | 参数序、`QUEUED,<seq>`、字段索引、4/5/6 终态均对；仅语义差别：write 只发数字 id、params 必须匹配 `param_words`、`targets=1` 且 `required` 默认 0xF 会被掩码拒绝 |
| 2 | 8 用例 + host 门禁 | 成立 | `DEFAULT_PATHS` 与 committed receipt 均含该文件；但 simulator 只命令名一致、报文形状不一致（N2/N3） |
| 3 | 文档口径与 catalog 38 | 成立 | 自数 yaml/C 表均 38 行；`-200/待 M2` 已删 |
| 4 | C11 补位提交单 | 成立 | 存在、明写待复核、README 已索引；旧「有结论无记录」债仍在 |
| 5 | OPEN 项描述 | 成立 | 单板单行、age 恒 0、无发布面/region、3/8 无分支、页面无 ControlPlane 引用；措辞瑕疵：wire 上无 `stale` 字段，"stale 恒 0" 是宿主派生 |

**新发现与处置**

| # | 发现 | 处置 |
|---|---|---|
| N1 | `wait_terminal` 无法等待「记录尚未产生」（空记录无应答→TransportError），且远端 slot 在 origin 取不到记录 | 已改：新增 `wait_aggregate()` 轮询 `CTRL:STATus?`（origin 聚合，覆盖 required）；`wait_terminal` 容忍无记录并标注仅本地 |
| N2 | simulator OBS 形状与固件不符；用例用未实装 key 3 断言 `valid=1` | 已改：schema/implemented 表；新增 key 3 stale 用例 |
| N3 | `write` 允许缺 params，实板会 `BAD_ARGUMENT` | 已改文档；用例改用 id 42（3 words） |
| N4 | `read_catalog.yaml` 与 `refmem_obs.h` 行格式双真源 | OPEN（M2 随固件批次；工作区已标 target/current，提交需 P3） |
| N5 | `refmem_cmd_reply_hil.py` 空记录注释过期 | OPEN（工具注释，M5 顺带） |
| N6 | `run_refmem_obs_tests.py` 仍写 `REFTMEM-READ-01` | 已改 `REFMEM-` |
| N7 | 迁移清单把 R1-2a（region/存储）与 R1-3（发布面）混写为「逐槽行」 | 已改：两线分开列 |
| N8 | 登记表声称 `build 20261109001932` 的 smoke，产物不可复跑 | 已改为「作者记录、产物未入库、不可复跑」 |
| N9 | demo `device.py` 目录仅 36 行、按名字查表，无法承接数字 id | OPEN（M5 页面迁移一并修） |

## 4.2 第二轮复核（只读子代理 `a1492f4e`，2026-11-09）

基线 HEAD `a685e06a` → 复核期间 `0c064db2`/`84302916`；未改文件、未跑硬件。

| 发现 | 处置 |
|---|---|
| M4 文档仍是旧口径（build `20261109001932`、`offset≥4`、`4-offset`） | 已改 build `20261109051835`、`DISTRIBUTED_REFMEM_NODE_COUNT`；四行冒烟归档 `out/refmem_read/obs_smoke_20261109.txt` |
| 提交单/登记表「逐槽行未做」与「M4 已实装」矛盾 | 已统一为「逐槽行 M4 已做，发布面/2 key/四板 HIL 未做」 |
| M2 决策表仍冻结 `field_id=0x100+key*16+word`，stale 语义冲突 | 已改为 `field_id=data_link_id` / `stale_window_us` |
| DataLink 为标量表且 14/14 已满；DELTA 只接受 width≤4、每源槽一个 mirror | 已写入架构/登记表：M2 是模型级改动（version/CRC/package、可能扩 instance 表），不是只补 runtime |
| M6 用 STOP-gated `trigger_deployment_snapshot()`，RUN 下会误拒 SWITCH_A/B | 已改为 `trigger_deployment_snapshot_cached()`，待新 P3 |
| 旧 `CONFigure:SWITch#` 文档未同步 | 已改 `SCPI_COMMANDS.md`：明确返回 `-200,"USE_CTRL_EXECUTE"` |
| 「P3 passed」口径 overreach | 统一改为「DPLL60_JOINT 构建凭证；未覆盖 OBS/开关路径」 |
| M4 receipt 的 package 已被 M6 构建覆盖 | **残余**：源码指纹仍绑 `142d70ff`，二进制路径不可复验 |
| M6 receipt 复用旧 trn03 矩阵 | **残余**：证据强度打折，行为证据需另补 |
| `required_mask`/`last_reason` 语义坑（非 required NACK 写 last_reason） | **残余**：调用方按 `required` + `ack/nack` 判读，不以 `last_reason` 单独判失败 |

## 5. C11 待复核项

1. 行 schema 取舍：采用逐槽行（契约文本）还是把契约改为「单板单行」并明确不做跨板读；
2. 发布面是否必须「唯一 writer + core0 发布 + dirty-key 增量」，以及 pilot region 的语义登记；
3. 读面不带 `target/required_mask`、不返回 NACK 的边界是否成立；
4. `valid=0 → age=0xFFFFFFFF/stale=1` 与 `stale_window_us` 的复用是否足够；
5. `wait_aggregate` 作为远端命令唯一等待语义，是否需要在契约里显式写出。

## 6. 结论

**待独立 C11 复核。** 本文件不冒充已通过；登记表此前记 `APPROVE_WITH_NOTES（2026-11-09）` 但无独立记录，本文件为补位提交，复核结论落盘后再回写登记表。
