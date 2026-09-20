# 工具域治理任务进度

Status: Active
Domain: Tools
Canonical: `docs/tools/DOCS_TOOLS_TASK_PROGRESS.md`
Related: `docs/tools/DOCS_TOOLS_ARCHITECTURE.md`, `docs/tools/DOCS_TOOLS_TODO.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-09-20

## 文档接口

本文只记录实施与验证事实，任务状态以 TODO 为准，治理语义以 Architecture 为准。每条记录引用稳定任务 ID，使用唯一 `TOOLS-PROGRESS-YYYYMMDD-NNN`，最新记录在上；轮转遵循文档自回归 C14/C15，历史失败和原始证据不得丢失。

## 当前 checkpoint

### TOOLS-PROGRESS-20260920-009：child RUN output 层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-RUN-001。
- 新增 `tools/workflows/vdc_joint_capture/run_output.py`，接管 119 字段 schema10 parser、timing snapshot/apply、四板 RUN prepare/parser 注册、output STOP-before-parent-barrier 和 STOP 后 RUN activity/retirement/timing/anchor 校验。外层 tracked lifecycle 继续作为 timing/delay 唯一恢复 owner；历史 RunProbe 内部 scope owner显式拒绝，联合 scope 只走已迁移的外部 sparse-scope。
- V4 manifest 将 archived `capture_run_output_schema10.py` 纳入固定 `run_output` migrated role，runtime entrypoint 下沉到 phase 层，动态 out closure 从 11 降到 10。默认 manifest/loader schema 有直接自回归；fresh closure actual=declared=10，missing / declared-not-loaded / migrated-loaded 为空。
- 软件与审查证据：tracked characterization 覆盖 schema10 合法行和八类 closed-set 拒绝、timing 读写、RUN prepare、parser 表、output STOP 顺序、STOP-only 四板校验和内部 scope 拒绝；最终宽组合 336 passed / 1 skipped，跳过项为 Windows 无符号链接权限。archived/tracked golden 的 FIELDS/WIDE 完全一致，119 字段合法行 parse dict 全等，八类负测两边同拒；四板 internal-seal golden 继续全等。独立复核在当前 tracked SHA 上 PASS；登记/catalog、编译、source length 与 whitespace 检查通过。
- 同指纹 P3 快照，非事实源：build `20260920121536` 的四板 quick P3 为 PASS_WITH_WARNINGS（INFO 23 / WARN 31 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-run-output-r1/p3-r1/`，staged receipt 同指纹。
- scope-off `joint-scope-off-r1` outer/inner PASS：V4 报告记录 10 migrated / 10 runtime 与 run-output SHA，internal health 通过、external SKIPPED、RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- scope-on `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 32.804 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复且无清理错误。硬件原件位于同任务目录，scope 结果不授予未采区间连续锁相或精确事件对应资格。
- 实现提交 `95f2504d`；TOOLS-WORKFLOW-RUN-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移剩余 10-file phase/follow/match 深层祖先；十份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-008：child runtime control / parallel START 基座迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-RUNTIME-001。
- 新增 `tools/workflows/vdc_joint_capture/runtime_base.py`，直接加载剩余 `capture_run_output_schema10.py` RunProbe 入口，接管历史 monitor 的 deadline/report、phase/RUN control 和 bounded parallel START。parallel START 从 V3 manifest 接受四板 allowlist，只使用已验证 persistent session 和互异 serial transport，保留 action budget、quiet barrier、四 worker join、排序/JSONL 及 fail-after-join 语义。
- V3 manifest 将 `external_monitor / parallel_startup / late_scope_adapter / outer_trial / outer_hil / legacy_scope_reader` 六份旧实现加入固定角色 migrated lineage；它们继续按路径/SHA 原位归档，但不再执行。runtime entrypoint 下沉到 RunProbe，out 动态闭包由 17 降到 11。fresh-process 动态检查遍历 factory 对象图和 `sys.modules`，actual=declared，missing / declared-not-loaded / migrated-loaded 全为空；原件在 `out/tools-governance/20260920/runtime-base-r1/closure.json`。
- 软件/审查证据：runtime fake characterization 覆盖 master/follower phase 与 RUN prepare 顺序、UID/port/allowlist、persistent session、distinct transport、deadline/action budget、quiet barrier、并发 join、单板 timeout 后全 join 和日志；宽组合回归 331 passed / 1 skipped，跳过项为 Windows 无符号链接权限。V3 frozen golden 为 9 migrated / 11 runtime，四板 archived/tracked parser/evaluation 逐字段全相等。独立复核 PASS，无剩余 finding；登记/catalog、编译、source length 与 whitespace 检查通过。
- 同指纹 P3 快照，非事实源：build `20260920113143` 的四板 quick P3 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-runtime-base-r1/p3-r1/`，staged receipt 同指纹。
- scope-off `joint-scope-off-r1` outer/inner PASS：V3 报告记录 9 migrated / 11 runtime 与 runtime-base SHA，internal health 通过、external SKIPPED、RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- scope-on `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 35.638 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复且无清理错误。scope 结果不授予未采区间连续锁相或精确事件对应资格。硬件原件位于同任务目录。
- 实现提交 `b6fe5cf8`；TOOLS-WORKFLOW-RUNTIME-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移剩余 11-file RunProbe/phase/follow/match 深层祖先；九份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-007：child internal summary / startup / seal 三层迁移

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-SEAL-001。
- 新增 `tools/workflows/vdc_joint_capture/internal_seal.py`，在 tracked factory 中重建 internal summary trace、可选 pre-START MODEL 省略和 sealed GUARD/parser/verdict 三层；联合 child 只从 manifest 的 `runtime_entrypoint` 加载下一层，不再 exec archived compatibility selector。V2 manifest 用固定角色 `internal_seal / quiet_startup / internal_summary` 保存三份原件路径/SHA，compatibility selector 必须绑定 internal_seal；三份 migrated source 与 runtime closure 不得重叠，out 动态执行闭包由 20 降至 17。
- 软件与等价性证据：tracked fake-runtime characterization 实际构造 SealProbe，覆盖四板 summary arm/parser 注册、optional MODEL 不发送、SUMMARY→GUARD 与 expiry 命令改写、quiet parallel START、STOP collect gate、cleanup 后资格重算；最终组合回归 321 passed / 1 skipped，跳过项为 Windows 无符号链接权限。frozen golden 使用上一批通过的四板 native SRAM 和 RUN raw，对 archived/tracked parser 及 `evaluate_board` 逐字段比较，四板全相等且 PASS；原件在 `out/tools-governance/20260920/internal-seal-r1/`。
- 独立审查先后发现并推动修复：golden 路径核对、固定 lineage 角色、compatibility selector 与 internal_seal 强绑定、真实 tracked 类 characterization、GUARD parser 表直接断言及 registry 债务措辞。最终复核 PASS，无剩余 finding；工具登记/catalog、编译、source length 和 whitespace 检查通过。
- P3 失败与复验，快照非事实源：修复前指纹 `p3-r1` 在 TRN-00 MARK 未形成完整 selected offset row 时拒绝，原件保留且不改写；最终 build `20260920105121` 的 `p3-r2` 为 PASS_WITH_WARNINGS（INFO 25 / WARN 21 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED，staged receipt 同指纹。
- 最终 scope-off 实测 `joint-scope-off-r1` outer/inner PASS：V2 manifest 在 child 报告中记录固定三角色和 17-file runtime closure，tracked adapter SHA 与源码一致，internal health 通过、external SKIPPED、RUN 内板端查询为零，未创建 scope/sample 目录；四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。
- 示波器恢复后补 scope-on 组合实测 `joint-scope-on-r1` outer/inner PASS：12 个新鲜窗口、48 RAW 块、steady qualification 通过，最大绝对相差 47.929 ns（快照）；RUN 内板端查询为零，四板 timing/delay 恢复，errors / cleanup_errors / restore_needed 均为空。scope 证据不授予未采区间连续锁相或精确事件对应资格。硬件原件均在 `out/HardwareAcceptance/20260920/tools-internal-seal-r1/`。
- 实现提交 `8a53402c`；TOOLS-WORKFLOW-SEAL-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一批迁移 `monitor_external`、startup 和 RunProbe 的更深基础链；17 份 runtime 原件继续按 SHA 准入，旧三份 migrated source 原位归档，不删除。

### TOOLS-PROGRESS-20260920-006：联合采样 outer 生命周期迁入 tracked workflow

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-LIFECYCLE-001。
- 新增 `tools/workflows/vdc_joint_capture/lifecycle.py`，接管父进程四板 UID→动态 COM 映射、串口 session、output/TDMA 双 STOP、identity/build/board order、RAM timing/delay 临时应用、child 日志与 timeout、最终恢复/读回和逐板关闭。联合入口 outer 分支不再 import adapter 或经 `base.external.base.trial.hil` 取得历史 runner；adapter 仅在已准入的 child 分支导入。
- 生命周期只迁入本工作流实际调用面，不带入历史 Flash STORE/reboot HIL。`scpi_serial.SerialSession` 新增向后兼容的实例级 response matcher，识别 timing 三元组及 signed delay，不再靠历史模块全局 monkey patch。报告保留 `DPLL_RAM_TIMING_TRIAL_V1`、`basis_script_sha256` 和原结果字段，新增 lifecycle version/source 及 restore detail。
- 安全语义：所有可达板收到两类 STOP 后才允许首个 query；RAM timing 与 delay 分别保持 dirty/uncertain，只有最终逐板 STOP、identity、PI/role/baseline、timing/delay 和 error queue 全过才清除。device-free 用例覆盖 child/restore/final drift/open/close/真实 subprocess timeout 等失败路径；独立复核在报告兼容、登记副作用和 restore false-negative 修复后最终 PASS。
- 软件验证快照，非事实源：最终组合回归 312 passed / 1 skipped，跳过项为 Windows 无符号链接权限；lifecycle/joint/TDMA 定向 146 passed。共享 SCPI 宽回归 521 / 522 passed，唯一失败是未修改的 `test_trn03_closed_loop.py` 对未修改 `application/src/app.c` 的既有文本断言，本批不把它改写为通过。工具登记/catalog、编译、source length 与 whitespace 检查通过。
- 硬件验收快照，非事实源：build `20260920094117` 的 scope-off 四板 P3 为 PASS_WITH_WARNINGS（INFO 23 / WARN 22 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-workflow-lifecycle-r1/p3-r1/`，staged receipt 与实现指纹一致。
- 首次实测 `joint-scope-off-r1` 保留 FAIL：NO1 并行 START 在原 action timeout 内返回 `<timeout>`，其余三板 OK；child 按既有规则 STOP，NO1 留下 `-200`，outer 记录 child 和 restore error，随后 final readback 证明四板 timing/delay 已恢复且 `restore_needed=[]`，失败未被覆盖。
- 同源码/凭证有界复验 `joint-scope-off-r2` PASS：九个 outer stage 全过，父报告 lifecycle SHA 与源码一致，四板 initial/final timing 和 delay 精确相同；child internal health 与 joint verdict 通过，RUN 内板端查询为零，external 为 SKIPPED，未创建 scope/sample 目录，errors / cleanup_errors / restore_needed 均为空。原件位于同任务目录。
- 实现提交 `f37c5fa0`；TOOLS-WORKFLOW-LIFECYCLE-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。下一步迁移不依赖示波器的 child Probe/基础 adapter；历史 outer 文件因 child 祖先仍 import 而暂留 manifest closure，不删除原件或提前宣称全链解除。

### TOOLS-PROGRESS-20260920-005：internal-seal 工作流输入显式化

- TODO task ID：TOOLS-WORKFLOW-001、TOOLS-WORKFLOW-INPUTS-001。
- 新增 `config/tool_workflows/vdc_joint_capture_internal_seal.json` 与 `tools/workflows/vdc_joint_capture/inputs.py`，将基础场景的 adapter entrypoint、完整历史可执行闭包、P3 receipt、stopped baseline、tap baseline 和四板 UID 顺序变为单一 tracked 输入清单。现有 CLI 路径与 `--bench-adapter` 保留，所选 adapter 必须与清单一致；vector/reference/gap 场景未混入。
- 准入顺序：解析 UTF-8 JSON、拒绝路径逃逸/重复、逐文件比对 SHA、校验 baseline 每板实际消费字段和唯一命令集合、校验 tap 主从请求、限定 quick-graded receipt schema 并验证完整证据/当前源码指纹/板卡顺序，全部通过后才动态 import adapter。父进程和子进程均在 `run()` 前写入 manifest、完整 closure 与摘要。
- 独立审查先后发现并推动修复：只 pin 最外 adapter、receipt schema 未显式限制、baseline/tap 深层字段浅校验、准入证据落盘过晚。最终复核 PASS，无剩余 finding；声明 closure 与既有基础链审计一致，当前 SHA 全部匹配，Git tracked 依赖由 P3 源码指纹覆盖。
- 软件验证快照，非事实源：最终组合回归 254 passed / 1 skipped，跳过项为当前 Windows 账户无符号链接权限；聚焦联合采样/输入/scope 组 159 passed。工具登记/catalog、JSON、编译和 whitespace 检查通过；旧 receipt 对新源码在 adapter import 和硬件前按预期拒绝。
- 硬件验收快照，非事实源：build `20260920085328` 的 scope-off 四板 P3 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0），DPLL 按 tdma-only 范围 SKIPPED；原件在 `out/HardwareAcceptance/20260920/tools-workflow-inputs-r1/p3-r1/`，staged receipt 与当前实现指纹一致。
- scope-off 基础工作流实测快照，非事实源：`joint-scope-off-r1` outer/inner 均 PASS，运行 60 s，manifest closure 在父子报告中均完整，RUN 内板端查询为零，external status 为 SKIPPED，未创建 scope/sample 目录；errors / cleanup_errors / restore_needed 均为空。原件在 `out/HardwareAcceptance/20260920/tools-workflow-inputs-r1/joint-scope-off-r1/`。
- 实现提交 `3f13f8d4`；TOOLS-WORKFLOW-INPUTS-001 关闭，TOOLS-WORKFLOW-001 保持 IN PROGRESS。当前示波器已移出台架，下一批只推进不需要示波器的 Probe/Trial、基础 adapter、静态检查或离线交接工作，不重跑 scope 硬件。

### TOOLS-PROGRESS-20260920-004：示波器叶端首批迁入标准目录

- TODO task ID：TOOLS-LAYOUT-001、TOOLS-WORKFLOW-001、TOOLS-SCOPE-001。
- 范围：联合采样当前 CLI 保持原路径；显式导入 `tools/common/hdo_scope/reader.py` 和 `tools/domains/vdc/scope_capture/sparse.py`，替换示波器运行路径的外置类继承与 monkey patch。板卡 adapter/Probe/Trial、START/STOP/恢复逻辑不迁移，本批不宣称 out 执行链全部解除。
- 用户追加要求：旧代码只归档、不删除。四份历史源文件原位保留，来源 SHA、迁移符号、目标与回滚方式登记在 `tools/domains/vdc/scope_capture/migration.json`；仍被旧板卡链导入的历史脚本不标为完全退役。

| 旧来源（均在 out/HardwareAcceptance/ 下原位保留） | 迁移符号 | 目标 | 调用/兼容和回滚 |
|---|---|---|---|
| 20260916/dpll-scope-hold-r1/capture_scope_r3.py | Scope / decode_block | tools/common/hdo_scope/reader.py | joint 改用显式 reader；旧源码及旧调用方保留，回滚本批 caller/registry 需重新验收。 |
| 20260918/dpll-phase-center-r1/capture_late_scope_schema10.py | ConfigurableScope | tools/domains/vdc/scope_capture/sparse.py | 保留 PRE 范围归一和内存配置行为；原 ResumeTrial 不迁移。 |
| 20260918/dpll-extended-window-r1/capture_startup_one_second.py | LongScope.write | 同上 | 保留 MAIN/OPC 顺序，取消模块 monkey patch；parallel_start 仍由旧板卡链承担。 |
| 20260919/dpll-checkpoint-r2/monitor_external.py | SparseScope | 同上 | 保留 RAW 缓存重置和 runtime WAV setter 等待；joint 的负阈值 fresh WAIT/STOP 和异步落盘保持原语义。 |

- 新增 `--scope-resource` / `--scope-serial`，传递到子进程并记录生效配置；默认仍为既有台架。新模块独立登记 EXPERIMENTAL，完整工作流保持 UNASSESSED，不自动授予跨设备/跨机支持。Scope 的 Python assert 改为显式错误，失败命令/坏块保留，关闭资源用 finally；这些差异由回归覆盖。
- 软件验证快照，非事实源：最终组合回归 242 passed / 1 skipped，跳过项为当前 Windows 账户无符号链接权限；真实离线对照 12 窗口 / 48 RAW 块、31,200,576 bytes，结果与原 phase.json 精确一致。原件来自 `out/HardwareAcceptance/20260920/joint-60s-r3/capture/`，新解码器不导入原脚本；新模块在无 out 的隔离目录可导入且不打开设备。文档回归 38 passed；四份旧源 SHA 全部一致。
- 审查失败与修复：初审发现 Scope 打开 VISA 后设置 timeout/chunk_size 失败会泄漏句柄；已将打开/设置纳入同一异常清理边界，分别关闭已得句柄，清理失败不替换原始异常。后续独立复核发现序列号曾在整个 IDN 字段集合中匹配，型号也可误通过；已改为精确比较 `*IDN?` 第三字段，并补厂商/型号/固件字段负测。修复后交回复核；两轮 CHANGES_REQUESTED 与最终结论均保留，不更改采样时序。
- 硬件验收快照，非事实源：最终源码指纹见 `config/hardware_acceptance/p3_acceptance_receipt.json`；build `20260920072724` 的快速四板 P3 为 PASS_WITH_WARNINGS（INFO 24 / WARN 21 / ERROR 0 / FATAL 0），P0/P3/T3 通过，TDMA 带告警通过，DPLL 按四板范围 SKIPPED。原件在 `out/HardwareAcceptance/20260920/tools-scope-leaf-r1/p3-r3/`，`check-staged` 已核对同指纹。
- 真实联合采样快照，非事实源：`joint-60s-r3` 外层及 joint verdict 均通过，12 个新鲜窗口和 48 个 RAW 块完整，11 个稳态窗口与首个收敛窗口均在 50 ns 带内；最大绝对相差为 37.596 ns。RUN 中板端查询为零，四板内部健康、STOP/恢复、异步落盘和示波器关闭完成，errors / cleanup_errors / restore_needed 均为空；不授予未采区间连续锁相或精确事件对应资格。原件在 `out/HardwareAcceptance/20260920/tools-scope-leaf-r1/joint-60s-r3/`。
- 失败证据：`joint-60s-r1` 因 HDO 未枚举而在 Scope 构造时拒绝，板卡完成 STOP/恢复，原目录保留；序列号修复后首次重跑因源码与旧 P3 凭证不匹配，在外层 preflight、输出目录创建和硬件运行前 fail-closed，因此没有 `joint-60s-r2` 硬件原件，本 checkpoint 只保留拒绝事实，不声称存在原始目录；错误选择链式复用摘要的 P3 尝试在硬件操作前拒绝，结构化拒绝原件保留在 `p3-r2/`。这些失败均未改写为通过。
- 工具治理证据目录 `out/tools-governance/20260920/scope-leaf-r1/` 包括软件回归、离线 golden、来源 SHA 和独立审查；TOOLS-LAYOUT-001 与 TOOLS-SCOPE-001 据此关闭。下一步仍是 TOOLS-WORKFLOW-001 的板卡生命周期与显式历史输入收敛，不能仅移动最外 adapter 来宣布全链迁移完成。

### TOOLS-PROGRESS-20260920-003：目录分层与迁移边界规划

- TODO task ID：TOOLS-LAYOUT-001；与 WORKFLOW、CONSOLIDATE 联动。
- 根据当前登记入口补充 TG-LAYOUT：业务域、公共能力、跨域工作流、构建、主机辅助分层；稳定验收/门禁/测试启动器原位保留，配置、测试和运行产物分别路由。DPLL 继续归 VDC，工具 ID 不随路径变化。
- 用户进一步要求“逐步将确认完成的工具移动至标准规划目录”；已补源位置准入、迁移、目标位置验证的顺序。未完成工具原位登记，已登记不等于已完成，目录迁入不自动授予 SUPPORTED。
- 给出联合采集、计量、示波器、TDMA、校准、SCPI、构建与主机辅助的首批目标方向；强调逐文件调用者映射、兼容包装、显式包入口、依赖方向与同指纹验收。OTA 保持当前边界，历史证据不搬动，未执行目录迁移或新建空目录。
- LAYOUT 保持 IN PROGRESS：总体规划已写入架构，首批逐文件旧/新路径、调用者、验证和回滚映射仍待实施前完成；不阻塞无关主线，不将所有存量搬迁作为前置条件。
- 本轮仅修改 tools 域三份文档，不改工具实现/登记/固件或特等席；文档门禁与独立复核结果见 `out/doc-audit/tools-layout-20260920/`。不产生新的硬件验收声明。
- 验证：独立审查 PASS；严格命名、文档自回归、检查器回归（38 passed，快照）、手工 hook、log-check、check-staged 通过。按审查建议明确登记/导航须在同指纹验收前同步。

### TOOLS-PROGRESS-20260920-002：全量登记与静态检查首批落地

- TODO task ID：TOOLS-INVENTORY-001、TOOLS-REGISTRY-001；TOOLS-CHECK-001 继续分阶段实施。
- 已建立根目录 `tools/tool_registry.json`、配套 schema 和 `tool_governance` 入口；文件归属精确到路径，工具组 ID 与显式 entrypoints 分离。根目录兼容包装归目标组，未移动原入口。`tools/README.md` 增加生成目录和治理导航。
- 盘点快照，非事实源：原有 263 个工具文件，含本轮设施后 268 个，登记为 98 个工具组；角色为 163 个入口、29 个模块、5 个兼容包装、53 个测试运行器、14 个资源、4 个说明文件。97 组为 UNASSESSED，新检查器为 EXPERIMENTAL；未把已登记升级为 SUPPORTED。静态检查的 194 条告警表示存量资格和依赖待审，不是硬件错误。
- inventory 用 Git 文件集合、字节哈希、Python AST 和位置线索盘点，不 import 被审工具；候选依赖不是完整执行图，out 输出路径也不自动算违规。check 校验 schema、文件归属、路径与入口、声明依赖、别名及证明引用；catalog 与 README 生成块一致性可显式检查，尚未接入 hook。
- 独立四板工作流盘点：最新实测基础 adapter 是 internal-seal，基础链依赖 20 个 out Python 文件，vector 专项额外 10 个；保守依赖图 83 节点/126 关系。vector 含 B 臂外参及参考中断注入，不能直接作为普通采样默认项。历史基线属于显式证据输入，源码、设备配置与证据须分别迁移，见 `workflow-audit.json` / `review-notes.txt`。
- 重复项复核：scope_dpll_capture、vdc_long_monitor、vdc_sync_metrology 分别是示波器快照、主动双板自检和离线计量，不能凭名称合并。未发现相同字节文件组；兼容包装有已确认转发关系，不当作重复实现删除。sd_fault_read 顶层串口访问/固定路径及 codex_watchdog 历史运行状态分别纳入专门待办。
- 失败与修复：初审复现文本末尾换行破坏目录、错误 schema 导致 KeyError、别名自环漏检；均修复，保留初审及最终 PASS 于 `registry-review.txt`。同时修正相对导入候选和二进制资源处理。Windows 首次依赖安装的 CLI launcher 写入失败，包导入可用；随后 `pip install --user -r ...` 返回成功，见 `dependencies-install.log`。首次 inventory 管道遇默认 GBK 输出，CLI 已显式输出 UTF-8。
- 软件验证快照，非事实源：静态登记和目录一致性通过；功能回归 95 passed / 1 skipped，跳过项为 Windows 无符号链接创建权限；文档回归 38 passed。日志见 `out/tools-governance/20260920/inventory-r1/tests-r4.log`、`docs-tests.log`，独立审查最终 PASS。
- P3 首轮失败：`out/HardwareAcceptance/20260920/tools-registry-r1/p3-r1/` 完成构建和四板 OTA，但 NO1 START 超时、记录样本为零，从板 START 成功；汇总未形成完整节点记录。结果 BLOCKED（INFO 8 / WARN 3 / ERROR 7 / FATAL 0，快照），四板清理后 STOP。保留原始记录及 `inventory-r1/p3-r1-failure-summary.json`，不宣称根因已修复。
- 有界复验：不改源码、不重刷，用 resume 复用本次 package/OTA，软件复位并复核 UID/build，沿用已知线序和原固定范围。`out/HardwareAcceptance/20260920/tools-registry-r1/p3-r2/alarms.json` 为 PASS_WITH_WARNINGS（INFO 25 / WARN 19 / ERROR 0 / FATAL 0，快照），四板 STOP 交接有效。原始 TDMA passed / realtime_gate_passed / closed_loop_passed 仍为 false，diagnostic_passed 为 true；DPLL 未执行。本结果仅支持固定范围快速验收，不授予严格实时或锁相资格。
- 凭证快照，非事实源：build `20260920021717`；源码 SHA256 `0dc1f0bfa9c1458e42dc2cbfd5a0777f045699c50ea39c87c0087e4a8601f548`，1333 个文件。凭证为 `config/hardware_acceptance/p3_acceptance_receipt.json`，软件与审计证据位于 `out/tools-governance/20260920/inventory-r1/`；源码及域文档分离交付。
- 提交与收口：实现和对应凭证已提交 `f9993de4`；tools 内用法说明/生成目录属于同一工具指纹，随实现交付，docs 域文件另交付。严格命名、文档自回归、`FORCE_DOC_GATES=1` 手工 hook、log-check、check-staged 与 staged whitespace 均通过，日志见同目录 `*-closeout.log` / `log-audit.log`。仅关闭文件盘点和初始登记，静态检查完整目标仍在推进。
- 下一 gate：本切片验收后推进 TOOLS-WORKFLOW-001，先消除基础采样链的临时可执行依赖，再处理带注入的专项模式；完整运行依赖、跨机交接与门禁接线仍未完成。

### TOOLS-PROGRESS-20260920-001：建立工具域治理三件套

- TODO task ID：TOOLS-DOCS-001。
- 用户要求：新增独立 tools 文档域，按 Architecture、TODO、Task Progress 分离治理语义、任务状态与实施证据。
- 已建立 `docs/tools/` 三份标准文件，并补总索引、canonical、进度路由及文档域结构规划。采用现行命名白名单支持的 `DOCS_TOOLS_*` 前缀，不为建域修改检查器。
- 盘点快照，非事实源：本轮前置读取时 `git ls-files tools` 返回 263 个文件、95 个含跟踪文件的子目录。`tools/README.md` 已存在使用说明；尚未发现统一工具登记。联合采集入口依赖外置 bench adapter，并硬编码历史 out 基线；存量完整分类与依赖审计尚未执行。
- 门禁差异：`.githooks/pre-commit` 当前 P3 触发 case 未覆盖全部 tools/tests/hook 实现变更，而 AGENTS 与 EXE-CHANGE-01 要求此类改动执行 P3；纳入 TOOLS-GATE-001，未在本轮修改规则或实现。
- 范围：仅文档；登记表、schema、检查器和工作流迁移仍为拟议。无新产品契约、registry 状态变更、特等席或 Core1 工作；不改 OTA。
- 验证与证据：独立复核无结构性阻断，按建议移除架构中的单次范围语句，并给 validation 旧入口补治理边界。docs_check 严格命名通过，doc_regression 通过且仅保留既有告警；检查器回归 38 项通过（本轮快照）。手工 hook 使用 `FORCE_DOC_GATES=1`，双文档门禁通过；log-check、check-staged 通过，后者明确无 staged 实现变更。本轮仅文档，不运行硬件 P3，不产生实现验收声明。输出目录 `out/doc-audit/tools-domain-20260920/`，含 `review.txt` 与门禁日志；本轮未提交。
- 失败/回退：首轮 doc_regression 拒绝空的归档索引标题（`gate-1.log`）；改为无归档说明，首次轮转再建立真实索引，复验通过。不移动或删除现有工具与证据。
- 下一 gate：TOOLS-INVENTORY-001 做全量归属和依赖盘点；建域完成不代表登记、检查器或迁移完成。

尚无轮转归档；首次轮转时建立归档索引表并登记真实文件。
