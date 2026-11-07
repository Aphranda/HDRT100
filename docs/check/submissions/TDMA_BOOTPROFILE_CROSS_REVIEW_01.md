# 校验提交单：单口启环 boot profile provisioning（TDMA / CALIBRATION）→ HAOFV 顶层

Status: Active
Domain: TDMA
Canonical: `docs/check/submissions/TDMA_BOOTPROFILE_CROSS_REVIEW_01.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/calibration/CALIBRATION_HANDOVER_PLAN.md`
Last updated: 2026-11-07

## 提交内容

把"严格单口启环"从台架编译开关改为**运行时 provisioning**，使 P3 验收镜像与台架镜像是同一份构建：
新增 `SYSTem:TDMA:BOOT:PROFile`（写）/`...:PROFile?`（读）/`...:PROFile:CLEar`（清），契约
`TDMA-BOOTPROFILE-01` 登记在 `DOCS_REGISTRY.md`，正文落在 `TDMA_DOMAIN_ARCHITECTURE.md`
「开机重建与单口启环」。配套：`tdma_boot_profile_store_next_generation/erase`、`BOOT:PLAN?` 追加
`boot_done`/`boot_next_step`（19 字段）、两套 host 测试 runner 接线、探针 `boot`/`boot write` 判据。

## 独立复核（C11，只读子代理 `3767015b`，2026-11-07）

结论：**REJECT**（首版）。理由：STOP-only 存在 fail-open 路径（环快照读不出时仍写 flash，flash AO 会
短停/重启环）；store 的"更新一代"在损坏记录/旧 schema 时无恢复路径。复核同时确认：generation +1 的
uint32 回绕被 `valid()` 拒绝、v1 地图只读/写入拒绝正确、commit 后 status 刷新正确、boot plan 只读一次
并冻结、无标定 ARM 路径成立、命令表与契约注册位置正确。

## 复核发现与处置

| # | 级别 | 发现 | 处置 | 状态 |
|---|---|---|---|---|
| F1 | BLOCKER | STOP-only fail-open：快照不可得时继续写 flash；快照失败与"环在跑"相关，AO 写入会 pause/restart 环 | 改为 fail-closed（快照不可得、`enabled` 或 `adapter_started` 任一为真即 `TDMA_BOOT_PROFILE_RING_RUNNING`），写/清共用同一门 | CLOSED |
| F2 | BLOCKER | `status.generation + 1` 在记录损坏/旧 schema 时永久卡死；rotate 分支不可达 | 新增 `tdma_boot_profile_store_next_generation()`（重扫两扇区取最新可读 +1，空=1，0xFFFFFFFF 拒绝）；commit 先擦除无法解析的扇区再追加 | CLOSED |
| F3 | MAJOR | 缺 `BRINGUP_NO_CAL` 的 profile 被接受，但本镜像无标定 stage，开机 ARM 必然失败并反复重放 | 写入口拒绝缺该位的 profile（`TDMA_BOOT_PROFILE_FLAG`） | CLOSED |
| F4 | MAJOR | 所有失败塌成同一错误串，主机无法区分"不支持/越界/写失败/校验失败" | 错误码分层：ARGUMENT/RING_RUNNING/RANGE/FLAG/UNSUPPORTED/GENERATION/FLASH/VERIFY/RECORD | CLOSED |
| F5 | MAJOR | "已 provisioning 可直接重跑 P3"缺屏障：boot 服务 3–33 s 有界重试会撤销主机的 STOP | `BOOT:PLAN?` 追加 `boot_done`/`boot_next_step`，文档把"`boot_done=1` 或 ≥33 s"写成 P0T 前置 | CLOSED |
| F6 | MAJOR | `node_count` 只受 profile 自身 8 节点上限约束，超过构建容量时到开机 TOPOLOGY 才炸 | 写入口再受 `PROJECT_NODE_CAPACITY` 限制 | CLOSED |
| F7 | MAJOR | playbook 仍写"刷 bench 镜像变体 `PROJECT_TDMA_GUIDE_STAGE_BOOT=ON`"，与唯一镜像原则冲突 | 该步骤标注为历史实验、不得用于单口验收；AUTOFORM 段落注明已被 `BOOT:PROFile` 取代 | CLOSED |
| F8 | MAJOR | 缺 clear/unprovision：provisioned 后无法表达"无 profile"，负向判据无法验证 | 新增 `...:PROFile:CLEar` + `tdma_boot_profile_store_erase()`（两扇区擦除、`loaded=0/generation=0/EMPTY`） | CLOSED |
| F9 | MAJOR | `test_tdma_boot_profile/plan/sequence/stage` 无 runner；新 SCPI 入口零用例 | 新增 `run_tdma_boot_plan_tests.ps1`、`run_tdma_boot_profile_store_tests.ps1`（v2 地图真实 commit/rotate/erase），均注册进 `run_host_unit_tests.ps1`；`tests/python/test_guide_boot_plan.py` 覆盖探针判据；顺带修正 `test_tdma_boot_sequence.c` 与现行序列（TOPOLOGY 在 STOP 前）不一致 | CLOSED |
| F10 | MAJOR | registry `check` 列声称的"四板无起环命令自组"P3 无法自动覆盖 | 契约 check 列改为"P3 覆盖命令面/错误码/不回归；零口自组与单口读回由 `guide_cmdplane_probe.py boot` 归档"。P3 不应执行 provisioning（会永久改变台架状态），该边界写入契约 | ACK（口径修正） |
| F11 | MINOR | `(int32_t)(new-old) < 0` 允许等代写入，与文档"严格更新"措辞不符 | 文档改为"严格大于最新可读记录"；命令由 `next_generation` 保证，store 的等代自愈路径保留 | CLOSED |
| F12 | NIT | `docs/calibration/CALIBRATION_TASK_PROGRESS.md` 写 `BOOT:PLAN?` 18 字段（实为 17，追加后 19） | 已改为 17→19 | CLOSED |
| F13 | NIT | "不得住宿半环" | 改为"不得停留半环" | CLOSED |
| F14 | BLOCKER（实板发现） | 四板首次 provisioning 全部 `-200,"TDMA_BOOT_PROFILE_FLASH"`：`FLASH_TRANSACTION_REQUESTER_CALIBRATION` 只被允许写 calibration store 子区，boot profile 在 CALIBRATION_NVS 的 4·ERASE 偏移，被事务策略以 PERMISSION 拒绝 | `flash_transaction_fb.c` 的 calibration requester 规则扩展为"calibration 子区 **或** boot profile 子区（v2 地图）"，两者按 flash map 静态断言互不重叠；host `run_flash_transaction_tests.ps1` 全绿 | CLOSED（待 P3 + 实板 provisioning 复验） |
| F15 | BLOCKER（实板发现） | 修完 F14 后仍 `-200,"TDMA_BOOT_PROFILE_RECORD"`：store 的 XIP 绝对地址只加 `..._STORE_OFFSET`（v2 地图上是 CALIBRATION_NVS 分区基址），漏了子区 `..._RELATIVE_OFFSET`（4·ERASE），于是 `scan_sector` 一直读 calibration store 的 TRN-03 记录（object 不匹配→ unusable），新加的"擦除不可解析扇区"逻辑也永远救不回来。host 单测的 stub 把绝对/相对坐标映射到同一窗口，故测不出来 | `sector_absolute()` 改为 `OFFSET + RELATIVE_OFFSET + sector·ERASE`；单测的 `REGION_BASE` 同步加 `RELATIVE_OFFSET`；store host 用例覆盖 commit/rotate/erase/next_generation 全绿 | CLOSED（待实板复验） |
| F16 | BLOCKER（实板发现） | `CLEar` 返回 `CLEARED` 但记录仍在（重启后 `loaded=1`），随后同参数 re-provision 报 `TDMA_BOOT_PROFILE_VERIFY`：flash 完成日志按请求指纹去重，指纹含 `store_generation`，clear 固定用 1、commit 用 profile 的 generation → 相同操作被当作 COMPLETED 重放而不做物理 IO | clear 以"被删除的 generation"作身份，并在擦除后回读两扇区（非空即 `VERIFY` 拒绝，不再谎报 CLEARED）；`commit/erase` 增加 `nonce`（SCPI 层传 boot uptime）折入身份，同参数重写不再被去重 | CLOSED（实板 `CLEar`+`PROFile` 通过：clear 后写回 generation=1、`loaded=1`） |

## 未纳入本次（预存在，记录为 OPEN）

- `tools/project/release_check/release_check.py` 要求 release preset 为 `v1_compat`，而全部 preset 是
  `v2_candidate`：即"release 验收镜像"与"可 provisioning 镜像"口径矛盾。与本次改动无关，但会阻塞产品
  路径，需 ARCH/OTA 域裁决。
- `BOOT:PLAN?` 的 `NODE_MISMATCH/SLOT_MISMATCH/REJECT` 分支在开机路径不可达（app 传入的 expected 正是
  profile 自身），已在契约正文注明，不删除分支（其它调用方复用）。

## 复审门

F1–F9 修改后需重跑：`host_acceptance.py run`（tools/tests 改动）、P3（固件改动）、四板 provisioning +
零口自组证据。复审结论在证据落盘后回填本节。

### 最终证据（build `20261107162842`，2026-11-08）

| 证据 | 路径 | 判据 |
|---|---|---|
| P3 | `out/HardwareAcceptance/20261108/p3-final2/` | graded outcome **PASS**，INFO 34 / WARN 0 / ERROR 0 / FATAL 0；OTA build=20261107162842；P0T 为同 build 独立重测 |
| 零口自组 | `out/calibration/guide-10k/evidence/20261108-013330-boot-final3/readback.json` | 四板 `action=START_REF/LISTEN`、`steps=8`、`fail_step=0`、`boot_done=1`、`up/down/adapter=1/1/1`，帧总量 68→72 |
| 单口 C/T | `out/calibration/guide-10k/evidence/20261108-013432-ct-final3/readback.json` | `command_port=COM5`；发送时只开 COM5；58.8 s 后四板 `PLAN? gen=950` |
| host | `config/hardware_acceptance/host_acceptance_receipt.json` | files=1165，77 passed |
| R26（M5） | `out/calibration/trn03-r26/summary.json` | 四板 `record_generation=1794072390, record_sequence=1, persisted_valid=1, reject=0` |

**台架恢复记录**：连续 factory 刷写后 OTA 在 durable=512 处失败（旧镜像同样复现）；用
`picotool_flash.py --full-erase` + factory 重刷 + `SYSTem:BOARD:NO <n>` 恢复四板，干净 flash 上 OTA 单板
PASS，随后四板 P3 一次通过。
