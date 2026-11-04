# T2 交接与自审清单（会话交接，2026-10-13）

Status: Active
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_HANDOFF_CHECKLIST.md`
Related: `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGISTRY.md`, `AGENTS.md`
Last updated: 2026-11-04

> 用途：把上一段会话（约七十余轮）的**工作状态、未完成项、流程铁律与验证方法**交给新任务；
> 新任务应**自行审查验证**，不要只信本文件的结论。

## 1. 环境事实

- 仓库 `D:\Work\HDRT100`（Windows）；台架四板：NO.1 `0010071E65B5CB38`(COM5)、NO.2 `FB276192BEF9CCE1`(COM4)、
  NO.3 `2BD5090FE009FA2A`(COM3)、NO.4 `A1E549202D18ED6A`(COM6)。
- 台架拓扑现状：四板各自串口；**未来**为"PC 只接一路串口，其余板卡经 RefMem 分布式控制"（用户 2026-10-13 指定）。
- 台架接线：**OUT4 与 IN1 已硬件回环**（无转台时用 OUT4 模拟转台输入；接真实转台时由转台驱动 IN1）。

## 2. 已闭环（**不要重做**，可自行复核）

| 项 | 结论 | 可核验处 |
|---|---|---|
| K-2 语义 | `prepare_deadlines_core0` 只跳过已被硬件接受的位（快照 `action_armed_mask|action_completed_mask`），移除"冻结即不发布"袢断 | 提交 `73dcd61a`；P3 分级凭证 PASS_WITH_WARNINGS(ERROR 0/FATAL 0) |
| 判据抽取 | 两个纯函数 `t2_deadline_position_accepted` / `t2_deadline_plan_current` | 提交 `bb06318f` |
| K-2 用例 | C1–C6 抽取式用例（驱动真实函数体） | 提交 `5d25ecfd`；`python -m pytest tests/python/test_trigger_t2_k2_recompute.py` |
| ① PIO 模式化 | **已合规**：persona 判据在 `sync_io_model_sched.c:931-934`，`:173` 停态装载、`:213` arm 只校验；非 PIO0 回退不在 T2 路径 | 提交 `a61436b0`；读上述行号 |
| ④ 转台通道 | 生产 IN1；台架 OUT4→IN1 回环模拟 | `TRIGGER_T2_ARCHITECTURE.md`、`TRIGGER_T2_TASK_PROGRESS.md` |
| 契约 1 | `ARCH-TRIGGERSLICE-01`（位置切片 10 B wire），status `pending` | `docs/check/DOCS_REGISTRY.md`；C11 提交单 `docs/check/submissions/TRIGGER_CROSS_REVIEW_01.md` |
| 工具链 | OTA 多板重枚举轮询（`DISCOVERY_SETTLE_S=60`）+ `ota_progress` 心跳 | 提交 `86732e17` |
| 接口域 | 三件套建立，`IF-001..003` | `docs/interface/SCPI_HOST_ARCHITECTURE.md` 等 |

## 3. 立即要做（按 ROI 顺序）

1. **收尾位置邮箱契约**（工作树有 2 个**未提交**文件）：
   - `docs/check/DOCS_REGISTRY.md` 中的 `ARCH-TRIGGERMAILBOX-01` 登记行（status `pending`）；
   - `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md` 中的"位置邮箱交接契约"小节（clause 载体）。
   - 步骤：**独立**交叉审核（禁自审自批，参照 `docs/check/DOCS_REGRESSION_PLAN.md` 的 C11 与
     `docs/check/submissions/README.md` 模板）→ 新建同类提交单（命名 `<域>_CROSS_REVIEW_<NN>.md`）→
     在 `docs/README.md` 加索引行 → 跑门禁 → 提交。**审核结论不论正负，均原样记录并按其实施修正。**
2. **`T2-AUDIT-03` 残项**：只欠"**TDMA 域一等席布局的联审结论**"（登记与 C11 已完成）。需用户指定由谁出结论。
3. **接口域 `IF-002`**（单串口控制面）：等用户 D1–D4 决定。**注意**：它属接口域，不要在 T2 的 ROI 审查中处理。

## 4. 流程铁律（上一段会话的教训，违反会直接失败）

1. **硬件验收用 workflow 前端**（有 tee 与流程视图）：`python tools/hardware_acceptance/hardware_acceptance_workflow.py --profile full [--run latest]`；
   旧入口 `p3_hardware_acceptance` 已归档到 `tools/archive/hardware_acceptance/`，只作历史兼容，不应再用于新验收。
2. **P3 运行期间绝不编辑任何被源码指纹覆盖的文件**（否则 `provisional receipt source fingerprint differs` 必 FAIL）。
3. **清理/杀进程与运行必须串行**；不要并发跑两个 P3（会互相杀掉）。
4. 定位"谁列出了某源文件"用 **`git grep`**：清单是根 `CMakeLists.txt` 的 `PROJECT_APP_SOURCES`。
5. 新文档命名：前缀必须 ∈ `tools/docs_check/docs_check.py` 的 `ALLOWED_PREFIXES`（含 `ARCH/TRIGGER/SCPI/VDC/TDMA/DOCS/REFMEM` 等，
   **不含 `INTERFACE`**），后缀 ∈ `ALLOWED_SUFFIXES`，前缀最多 3 段；新增 `.md` **必须**加 `docs/README.md` 索引行。
6. 契约登记：`clause_loc` 文档必须 `Status: Active/Frozen`；新登记 status 用 `pending`；契约 id 形如 `<DOMAIN>-<TOPIC>-<NN>`
   （具体正则以检查器为准，已不再按前缀白名单）；登记须 **C11 独立审核**且**禁止自审自批**。
7. 控制台输出保持**纯 ASCII**（GBK 编码会崩）；提交信息避免非 ASCII 引号。
8. `*_TASK_PROGRESS.md` ≤200KB、条目**倒排**（最新在最上，新条目写在 `## 当前 checkpoint` 之后）；
   下沉长记录到 Task Progress 以维持**单一事实源**（C12）。

## 5. 自审方法（新任务请据此**验证**，不要只读结论）

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_trigger_t2_k2_recompute.py tests/python/test_trigger_t2_freeze_abandon.py -p no:cacheprovider -q
git log --oneline -30
git status --short
```
- 逐条复核 §2 的"可核验处"（提交号 / 文件行号 / 契约行）；
- 复核 §3.1 的 2 个未提交文件是否仍在树上；若已提交，核对提交信息与 C11 记录是否一致。

## 6. 已知未做（诚实清单）

- `IF-002` 的完整设计（(b) RefMem 控制意图 + delta）**只在会话讨论过，未落文档**；
- RefMem 缺一条 `_Static_assert(sizeof(refmem_vector_table_t) == DISTRIBUTED_REFMEM_TABLE_SIZE)`；
- 参考实现分支 `origin/feature/node-sequence-reservation-trigger` 的 SEQUENCE 全域与其 tkinter 调试界面**未移植**。
