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
| 单板 smoke | 有读数 | `control_plane_smoke.py`、build `20261109001932` |
| 逐槽行 / 发布面 / 2 key / 四板 HIL | **未做** | 架构 §「未实装」 |

## 3. 审计发现与处置（2026-11-09）

| # | 发现 | 处置 | 状态 |
|---|---|---|---|
| 1 | 契约写逐槽行 `slot,valid,epoch,seq,age_ms,stale,<schema>`，实现只返回本板单行 `key,offset,count,valid,age_ms,<schema>` | 契约文本已标注「当前实装为本板单行」；逐槽行列入 M1/M4 | OPEN（M1/M4） |
| 2 | `ControlPlane.wait_terminal` 调不存在的 `SYST:REFMEM:COMMAND:STATus?` | 改读 `SYST:COMMAND:TARget? <slot>`，终态 4/5/6 | CLOSED（本轮） |
| 3 | `control_plane_smoke.py` 仍把 5/7 key 当未实装 | 改为 PILOT={1,2,4,5,6,7}、NOT={3,8} | CLOSED（本轮） |
| 4 | `SCPI_COMMANDS.md` 写「实现待 M2，当前返回 -200」 | 改为「pilot 6/8 已实装」并补逐槽行说明 | CLOSED（本轮） |
| 5 | 登记表 READ-01 证据写「pilot 4 key / 待 host / 待 smoke」 | 回写 6/8 + host/smoke 已完成 + 剩余项 | CLOSED（本轮） |
| 6 | 契约 id `REFTMEM-READ-01` 与登记 `REFMEM-READ-01` 混用 | READ-01 三处改为 `REFMEM-`；历史 `REFTMEM-LEASE/IDLE` 为既有登记，不批量改 | PARTIAL（READ-01 已一致） |
| 7 | `OBS?` 硬编码 `4u` 而非 `DISTRIBUTED_REFMEM_NODE_COUNT` | 列入 M4 | OPEN（M4） |

## 4. C11 待复核项

1. 行 schema 取舍：采用逐槽行（契约文本）还是把契约改为「单板单行」并明确不做跨板读；
2. 发布面是否必须「唯一 writer + core0 发布 + dirty-key 增量」，以及 pilot region 的语义登记；
3. 读面不带 `target/required_mask`、不返回 NACK 的边界是否成立；
4. `valid=0 → age=0xFFFFFFFF/stale=1` 与 `stale_window_us` 的复用是否足够。

## 5. 结论

**待独立 C11 复核。** 本文件不冒充已通过；登记表此前记 `APPROVE_WITH_NOTES（2026-11-09）` 但 `docs/check/submissions/` 无对应记录，本文件为补位提交，复核结论落盘后再回写登记表。
