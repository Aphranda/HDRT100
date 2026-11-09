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
- 8/8 key 的本地 provider（`switch1/2`、`guide_plan`、`boot_plan`、`ring_status`、`cal_status`，以及 `trigger_state`/`node_health`，`6c28df1c`）；
- `tools/refmem/control_plane.py` 的 `read/write/wait_terminal` 与 `tools/refmem/control_plane_smoke.py`。

## 2. 当前实现状态（2026-11-09，build `20261109063728`）

| 项 | 状态 | 证据 |
|---|---|---|
| 读目录 8 key | 已生成 | `refmem_read_catalog_table.c`、`test_refmem_read_catalog.c` |
| `OBS?` 接线 | 已实装 | `scpi_refmem_snapshot_commands.c:1325-1430` |
| key provider | 8/8 | `836dae7a`、`c6f2c76e`、`3624f322`、`6c28df1c` |
| host 用例 | 通过 | `test_refmem_obs.c`、`test_refmem_control_plane.py`（8 passed） |
| 单板 smoke | 有归档 | `out/refmem_read/obs_smoke_20261109.txt`（build `20261109051835`：`OBS? 6,0,4` 四行、`OBS? 1,0,2` 两行） |
| 逐槽行 / 8 key / 发布面 / 四板 HIL | 逐槽行 **M4 已做**；8/8 key 本地 **M3 已做**；发布面/四板 HIL 未做 | 架构 §「未实装」 |

## 3. 审计发现与处置（2026-11-09）

| # | 发现 | 处置 | 状态 |
|---|---|---|---|
| 1 | 契约写逐槽行 `slot,valid,epoch,seq,age_ms,stale,<schema>`，实现只返回本板单行 `key,offset,count,valid,age_ms,<schema>` | M4 已实装逐槽行（本地 valid、对端 stale）；真实 `seq/age` 待 R1-3 | CLOSED（M4，seq/age 待 M2） |
| 2 | `ControlPlane.wait_terminal` 调不存在的 `SYST:REFMEM:COMMAND:STATus?` | 改读 `SYST:COMMAND:TARget? <slot>`，终态 4/5/6 | CLOSED（本轮） |
| 3 | `control_plane_smoke.py` 仍把 5/7 key 当未实装 | 曾改为 PILOT={1,2,4,5,6,7}、NOT={3,8}；**现已 8/8（PILOT=1..8、NOT={}）** | CLOSED（历史，已更新） |
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
| M4 文档仍是旧口径（build `20261109001932`、`offset≥4`、`4-offset`） | 已改 build `20261109063728`、`DISTRIBUTED_REFMEM_NODE_COUNT`；8-key 冒烟归档 `out/refmem_read/obs_8keys_20261109.txt` |
| 提交单/登记表「逐槽行未做」与「M4 已实装」矛盾 | 已统一为「逐槽行 M4 已做、8/8 key M3 已做；发布面/四板 HIL 未做」 |
| M2 决策表仍冻结 `field_id=0x100+key*16+word`，stale 语义冲突 | 已改为 `field_id=data_link_id` / `stale_window_us` |
| DataLink 为标量表且 14/14 已满；DELTA 只接受 width≤4、每源槽一个 mirror | 已写入架构/登记表：M2 是模型级改动（version/CRC/package、可能扩 instance 表），不是只补 runtime |
| M6 用 STOP-gated `trigger_deployment_snapshot()`，RUN 下会误拒 SWITCH_A/B | 已改为 `trigger_deployment_snapshot_cached()`，待新 P3 |
| 旧 `CONFigure:SWITch#` 文档未同步 | 已改 `SCPI_COMMANDS.md`：明确返回 `-200,"USE_CTRL_EXECUTE"` |
| 「P3 passed」口径 overreach | 统一改为「DPLL60_JOINT 构建凭证；未覆盖 OBS/开关路径」 |
| M4 receipt 的 package 已被 M6 构建覆盖 | **残余**：源码指纹仍绑 `142d70ff`，二进制路径不可复验 |
| M6 receipt 复用旧 trn03 矩阵 | **残余**：证据强度打折，行为证据需另补 |
| `required_mask`/`last_reason` 语义坑（非 required NACK 写 last_reason） | **残余**：调用方按 `required` + `ack/nack` 判读，不以 `last_reason` 单独判失败 |

## 4.3 第三轮复核（只读子代理 `33b11994`，2026-11-09）

| 声称 | 结论 | 处置 |
|---|---|---|
| cached snapshot + P3 build `20261109055830` 源绑定 | 成立（指纹可复算） | 凭证保留；但**不覆盖开关功能路径**（`unverified_scope` 含 sync I/O trigger paths） |
| boot wait 35 s | 实现成立；**“boot_done 陈旧”归因不成立**（BOOT:RESET 清 BSS） | 已改注释与判据：`fail_step=0 && boot_done=1 && half_ring=0`，pending 非空抛 `AcceptanceError`（`1c6a16c7`） |
| 无 deployment ⇒ 全板 reason 12 | **已更正**：`9aeb173d` 后为 reason 13（CONFIG_VALIDATION_FAILED）；12 仅用于角色不符 | SWITCH_A 正向仍需 stage+activate 部署后补 |
| “广播曾写 NO1/NO3 OUT1-OUT3” | 部分成立（机制在，历史读回无归档） | 不作为完成证据 |
| M2 方案可照写 | **不成立** | 提案已重写：A′/B′ 待裁决；`0x100` 与“每周期通用 field/value”均证伪 |

**新发现**：`BOOT:PLAN?` 字段数文档漂移（19 vs 20、0/1-based 混用）；`wait_boot_replay` 每 2 s `discover()` 会重扫所有串口。**reason 语义稀释已由 `9aeb173d` 修正**（BUSY→6、无部署→13、非 owner→12）。

## 4.4 第四轮复核（只读子代理 `d33f58cb`，2026-11-09）

| 发现 | 处置 |
|---|---|
| 8/8 已实装，但 host `control_plane_smoke.py`、simulator、`test_read_unimplemented_key_is_stale` 仍把 3/8 当未实装 | 已改：PILOT=1..8、`OBS_IMPLEMENTED`=all、测试改为 key 3/8 valid（host 12 passed） |
| 多处文档仍写 6/8（arch、SCPI、README、迁移清单、登记表、提交单） | 已统一为 8/8 |
| key 3 语义（state/run_state/epoch）未文档化，且行头 epoch=1 与 `epoch=run_id` 同名不同义 | 架构新增「key 3/8 语义」段 |
| 历史记录“无部署⇒12”已被 `9aeb173d` 取代 | 提交单 §4.3、§4.2 已更正为 13 |
| commit `9aeb173d` 用 `REFMEM-READ-01`，实际属控制/入口面 | **OPEN**：后续 M6 提交改用 `REFMEM-CTRL-01`/`REFMEM-ENTRY-01` |
| `BOOT:PLAN?` 字段数 19/20 与 0/1-based 漂移 | **OPEN**：M3 文档批次统一 |
| 8-key 冒烟晚于 receipt 6 s、reason 13 现场未归档 | 8-key 已归档；reason 13 补归档 `out/refmem_read/switch_reason_20261109.txt` |
| `wait_boot_replay` 每 2 s `discover()` 副作用 | 已改为 discover 一次、集合不匹配才刷新（`1c6a16c7`）；残余为每 2 s 轮询 |
| BUSY→6 / 角色→12 无 host 用例 | **OPEN**：需 host 侧 reason 映射用例 |

## 4.5 第五轮复核（只读子代理 `d5131cfa`，2026-11-09）

| 发现 | 处置 |
|---|---|
| HIL 归档缺 stage/activate 与 `DEPloyment?` 读回 | 已补采：四板 `DEPloyment?` 均 `active=1/gen=1800000001/crc=1450950601` 且 role/slot 正确；见 `REFMEM_SWITCH_HIL_ANALYSIS_01` |
| 测试部署可能残留 | 已逐板 `DEPloyment:CLEar`，`active=0` |
| `output_mask` 只做形状校验，SWITCH_B 行 output=1 与 3 位 SIO 驱动不一致 | 记为部署/资源命名空间待澄清项，不阻塞 owner 判定 |
| build `20261109072222` 无 receipt 绑定 | 记残余；其固件源指纹与 `20261109063728` 一致 |
| 广播聚合无归档 | 已归档 `-200,"NOT_ONLINE_SAFE"`；聚合留 M7 |
| 进度日志历史行仍写“未配置→12” | 已加注“历史口径，后由 9aeb173d 改 13” |

## 4.6 第六轮复核（只读子代理 `455f2a76`，2026-11-09）

| 发现 | 处置 |
|---|---|
| `check_page` 未校验本地 `valid=1`、page 头/count、valid∈{0,1}、本地 fresh | 已修：本地必须 valid=1/stale=0/age≤预算；page key/offset/count 与请求一致；新增 9 单测 |
| JSON 未落行级证据，PASS 不能证明“本地 valid=1” | 已落 `local{valid,age_ms,stale}`、`peer_stale`、`build` |
| registry `operation=OFFLINE`、validation 自指入口 | 已改 `DEVICE_READ`、指向 `tests/python/test_refmem_obs_matrix.py` |
| `REFTMEM-READ-01` 拼写、`M7/M8` 中 M8 未定义 | 已改 `REFMEM-READ-01`、只写 M7 |
| 登记表证据列未回填矩阵 | 已回填四板 8-key 矩阵与 build |
| pending P3 receipt `passed=true` 但 `tdma_stage_passed=false`、引用未跟踪 bench 配置 | **残余**：不作为 FULL PASS 提交；需重跑或明确降级 |

## 5. C11 待复核项

1. 行 schema 取舍：采用逐槽行（契约文本）还是把契约改为「单板单行」并明确不做跨板读；
2. 发布面是否必须「唯一 writer + core0 发布 + dirty-key 增量」，以及 pilot region 的语义登记；
3. 读面不带 `target/required_mask`、不返回 NACK 的边界是否成立；
4. `valid=0 → age=0xFFFFFFFF/stale=1` 与 `stale_window_us` 的复用是否足够；
5. `wait_aggregate` 作为远端命令唯一等待语义，是否需要在契约里显式写出。

## 6. 结论

**待独立 C11 复核。** 本文件不冒充已通过；登记表此前记 `APPROVE_WITH_NOTES（2026-11-09）` 但无独立记录，本文件为补位提交，复核结论落盘后再回写登记表。
