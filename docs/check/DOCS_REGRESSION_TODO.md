# 文档自回归体系实施待办

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGRESSION_TODO.md`
Related: `docs/check/DOCS_REGISTRY.md`, `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`
Last updated: 2026-08-22

> 方案: `doc-skill/方案_文档自回归体系.md`（工作区，T13 归档入本项目）
> 工作流: 方案审核 → 待办 → 逐条执行 + 每步反馈
> 状态: `done` / `in_progress` / `pending`

## 执行状态

| # | 待办 | 状态 | 验证结果 | 反馈 |
|---|---|---|---|---|
| T1 | 建 `docs/check/DOCS_REGISTRY.md`（5 字段元数据 + 5 契约 + 落点表） | done | docs_check --strict-names 全绿（94 文件 0 FAIL） | 发现并修复 3 个集成点：登记表被旧检查器拒收（元数据/索引/命名）、PRODUCT_BOARD_MIGRATION_PLAN 元数据出窗口、RISK_REVIEW 命名不合规 |
| T2 | 建 `tools/doc_regression_check.py`（环1+环2，--scope） | done | --registry 5 契约全绿；--freshness 正确放行（顶层 6 天 < 7 天阈值） | 验证抓到 2 个问题并修复：①freshness 排除集含 "docs" 导致全库被排除（逻辑 bug）；②code_anchor 指向不存在的 tdma_ring_profile.h/refmem_frame.h，改为真实文件 tdma_profile.h/refmem_sync.h |
| T3 | 建 `tests/python/test_doc_regression.py`（5 用例） | done | pytest 8 通过（5 新 + 3 旧） | 2 个真问题：①沙箱拦 .pytest_cache/tmp（用 --basetemp 绕开验证）；②check_freshness 签名加 scope 后测试未同步，已修 |
| T4 | 建 `.githooks/pre-commit` + `git config core.hooksPath .githooks` | done | hooksPath=.githooks；hook 实跑双检查全绿 exit 0 | 无问题；阻断性测试留到 T7 验收 |
| T5 | 建 `.agents/skills/doc-self-regression/SKILL.md` | done | 已创建，frontmatter 触发 + 工作流 | 路径用真实 DOCS_REGISTRY.md；补充 pytest 的 --basetemp 经验 |
| T6 | 根 `AGENTS.md` 追加文档维护章节 | done | 已创建（原无 AGENTS.md，新建含常设命令） | — |
| T7 | 全量验收（检查器 / pytest / 门禁三连） | done | 检查器全绿 + pytest 8 通过 + 坏文档 commit 被拒（3 FAIL） | 门禁阻断实测成功；git index 有残留 AD 状态（IDE/先前暂存），git reset 规范化 |
| T8 | 环2 扩展：孤儿条款 + Draft 文档禁止登记 | done | 检查器 3 项全绿（freshness/registry/orphan）+ pytest 8 通过 | 发现 STATUS_RE 缺 re.MULTILINE 导致 Status=missing，已修；测试 fixture 缺 Status 行被新检查抓到（自回归生效） |
| T9 | 检查器加固：预索引 + 排除 build + 编码防御 | done | 双检查合计 1.17s（C6 ≤5s ✓）；GBK 文件读取不崩溃 | T2 基座已含预索引/SCAN_EXCLUDE_DIRS/errors=ignore，实测确认 |
| T10 | 逃生门留痕：hook 日志 + git log 比对 | done | 正常 OK；模拟绕过（标记改1h前）→ WARN 2590s；恢复后 OK | 实现为 --log-check 独立审计项；hook 每次成功写 .git/doc-verify-last 标记 |
| T11 | 提交单归档目录 + 模板 | in_progress | — | — |
| T12 | 检查器交叉审核（独立评审 doc_regression_check.py） | done | 10 测试通过 + 检查器全绿 | 交叉审核发现 4 问题：①parse_date 畸形日期崩溃（P1 修复）；②clause_loc 缺失掩盖 code_anchor 错误（修复）；③Windows 控制台 GBK 乱码（stdout reconfigure）；④孤儿/畸形日期无测试（补 2 用例） |
| T13 | 方案收尾：v3 转 Active 归档入 docs/check/ | done | 96 文件双检查全绿 + 10 测试通过 | 归档发现 6 处旧路径引用（DOC_REGISTRY→DOCS_REGISTRY），批量替换 |
| T14 | 总结经验：执行中发现的问题与解法汇总成文档 | done | `docs/check/DOCS_REGRESSION_REVIEW.md` 已归档，docs_check 全绿 | 汇总 8 轮验证抓到的 10 个真问题 + 5 类经验 + 维护建议 |
| T15 | 形成文档自回归 skill 插件（精修 SKILL.md + 工具 + 模板 + 打包） | done | 插件含 11 个文件；最终验收全绿：docs_check 97 文件 0 FAIL + doc_regression 3 项 OK + pytest 25 通过 + pre-commit OK + 逃生门 OK | SKILL.md 纳入实施经验；README 含复用指南（改 3 个常量即可移植） |

## 完成状态

- 2026-08-19：T1-T15 全部完成。最终验收：
  - `docs_check.py --strict-names`：97 文件，0 FAIL 0 WARN
  - `doc_regression_check.py`：freshness + registry + orphan 全 OK
  - pytest：25 通过（含检查器自回归测试）
  - pre-commit 门禁：实测阻断坏提交成功
  - 逃生门审计（--log-check）：正常
- 待办未纳入本次：环3 数字单一来源检查、verify-doc-crosscheck、顶层 HAOFV 刷新（见 DOCS_REGRESSION_PLAN.md §6）→ 已补建为下一期 T16-T21

## 下一期（2026-08-19 补建，未纳入本次 T1-T15）

| # | 待办 | 来源 | 截止 |
|---|---|---|---|
| T16 | 环3 数字单一来源检查：文档代码块 `#define X N` vs 代码实际值 | 方案 §6"下一期" | done（2026-08-19） | 实现 `--constants`（opt-in，快照标注豁免）；实测扫出 legacy 文档未验证 WARN（合理）；2 测试 |
| T17 | verify-doc-crosscheck：登记契约"域文档描述 vs code_anchor 实际值"自动比对 | 方案 §6"下一期" | done（2026-08-19） | 实现 `--crosscheck`（关键字在锚点文件出现，WARN 级启发式）；5 契约全过；1 测试 |
| T18 | 顶层 `HAOFV_ARCHITECTURE.md` 刷新：bump 版本 + 错误码表补 TDMA 段 + 修"190 规则"残留 + 5 契约在顶层可见 | C8 硬约束 | **Codex 任务**（任务单 `docs/temp/HAOFV_REFRESH_PLAN.md`，含验收检查表 §五 + 交叉审核 §六） | ⚠️ 2026-08-26（7 天窗口） |
| T19 | 逐级核验流程首次实操：首份提交单 demo + 结论回写登记表 | 用户"逐级往上提交核验" | done（2026-08-19） | 首份提交单 `TDMA_CROSS_REVIEW_01.md`（HAOFV-879 seqlock 偏差，ACCEPT_WITH_DEVIATION + 交叉审核 PASS_WITH_NOTE）；命名规范修正：提交单支持数字序号后缀（is_allowed_name 容错 + 测试） |
| T20 | docs_check 白名单变更补测试：PRODUCT/REGISTRY/REVIEW 加入后 test_docs_check 补断言（C5 三同步） | 执行中发现遗漏 | done（2026-08-19） | 新增 `test_docs_check_accepts_added_allowlist_names`，4 测试过 |
| T21 | 项目↔harness 插件同步规则：SKILL.md 更新后同步到 harness 副本的命令/约定 | 插件安装引出 | done（2026-08-19） | 实现为 `--skill-sync`：live 脚本+模板→项目 skill→harness skill 三级镜像；全量检查自动含 skill-sync 自检（自我指涉门禁）；pytest 26 通过 |
| T22 | **skill 副本同步机制**（自我指涉）：`--skill-sync` 一键同步 + 全量检查含 skill-sync 自检 | 三副本漂移风险 | done（2026-08-19） | 漂移实测：篡改 harness 快照 → FAIL 拦截 → --skill-sync 恢复 → 全绿 |
| T23 | **域文档标准三件套**：Architecture/TODO/Task Progress 事实边界、互相引用和禁止复制规则 | DOCS-FLASH-01 / 用户域内约束 | done（2026-08-22） | 已写入 DOCS_REGRESSION_PLAN C12、REVIEW §8，并在 Flash 三份域文档落点；registry 新增契约并保持 pending |

## 每步反馈记录

### T1（2026-08-19）
- 创建登记表，改名 DOC_REGISTRY→DOCS_REGISTRY 对齐命名约定
- 修复 3 个集成点（见上表），docs_check 全绿
- 反馈到方案：G1 实际发生（登记表被 docs_check 拒收），处置有效

### T2（2026-08-19）
- 检查器验证抓到 2 个真问题：freshness 排除逻辑 bug + code_anchor 指向不存在的文件
- 修复后 --registry/--freshness 全绿
