# 文档自回归体系实施经验总结

Status: Active
Domain: Documentation Governance
Canonical: `docs/check/DOCS_REGRESSION_REVIEW.md`
Related: `docs/check/DOCS_REGRESSION_PLAN.md`, `docs/check/DOCS_REGRESSION_TODO.md`
Last updated: 2026-08-22

> 本文件总结 2026-08-19 实施 T1-T13 过程中发现的全部问题与解法，供后续维护和 skill 复用。

## 1. 核心结论：自回归验证每次都能抓到真问题

实施过程中，每一轮"写完就验证"都抓到了实际缺陷，没有一轮是空转：

| 轮次 | 抓到的问题 | 严重度 |
|---|---|---|
| T1 | 新登记表被旧检查器拒收（缺 Related 字段 / 未入索引 / 命名不合规） | P1 集成 |
| T2 | freshness 排除集含 "docs" 把全库排除（逻辑 bug） | P1 |
| T2 | code_anchor 指向不存在的文件（方案猜测 vs 代码现实） | P1 |
| T3 | check_freshness 签名变更后测试未同步 | P2 |
| T8 | STATUS_RE 缺 re.MULTILINE → Status=missing | P1 |
| T8 | 测试 fixture 文档缺 Status 行被新检查抓到 | P2（自回归生效） |
| T12 | parse_date 畸形日期（2026-13-99）崩溃 | P1 |
| T12 | clause_loc 缺失时 continue 掩盖 code_anchor 错误 | P2 |

**经验：验证不是走过场，是发现真实缺陷的机制。**

## 2. 集成点问题（最容易翻车的类型）

新文件进入既有体系，必须过旧检查器的三道关：

1. **元数据**：5 字段必须在前 7 行内（`header_window = lines[1:8]`）——字段多了会被挤出窗口（PRODUCT_BOARD_MIGRATION_PLAN 的 Target/Source of truth 占位导致 Last updated/Related 出窗）
2. **索引**：docs/README.md 必须列全所有 .md（含子目录）
3. **命名**：前缀/后缀必须进 `ALLOWED_PREFIXES/ALLOWED_SUFFIXES` 白名单

**解法**：新文件按既有约定命名（如 DOC_* → DOCS_*）、元数据顺序固定、白名单只做增量扩展。

## 3. 检查器自身的 bug 类型（交叉审核 T12 的价值）

- **日期解析不校验合法性** → `datetime.date(2026,13,99)` 崩溃。任何从文档提取的值都必须验证后再用
- **正则锚点**：`^` 需要 `re.MULTILINE` 才匹配行首；文档扫描类正则最容易漏
- **排除集语义**：`"docs" in exclude_dir` 会误伤 `docs/` 顶层——排除目录必须用精确路径/次数判断
- **错误掩盖**：一个检查失败后 `continue`，会跳过同一行的其他检查——每个字段独立报告

## 4. 测试先行的价值

- pytest 是检查器的"自回归测试"：改检查器签名（scope 参数）、改检查逻辑（Status 检查）时，旧测试立刻暴露破坏
- 测试 fixture 要与检查器假设一致（fixture 文档也要满足 Status 要求）
- 新增检查必须带正反用例（通过 + 拒绝），否则等于没写

## 5. 环境/工具链坑

| 坑 | 解法 |
|---|---|
| 沙箱拦 `.pytest_cache`/tmp 目录（WinError 5） | pytest 加 `-p no:cacheprovider --basetemp <可写路径>` |
| Windows 控制台 GBK 输出中文乱码 | `sys.stdout.reconfigure(encoding="utf-8", errors="replace")` |
| git index 出现 `AD` 残留（IDE/先前暂存 + 文件改名） | `git reset` 规范化（只清暂存区，不动工作区） |
| pre-commit 钩子 Windows 执行 | `#!bin/sh` + 无 BOM + LF 行尾；`git config core.hooksPath .githooks` |

## 6. 流程经验（对方案的反哺）

- **待办文件是执行跟踪的关键载体**：独立 `DOCS_REGRESSION_TODO.md`（状态 + 验证结果 + 反馈三列）让每一步可见、可追溯
- **"每步反馈"要有真实内容**：验证结果 + 发现问题 + 修复方式，不只是"完成"
- **方案里的文件名/锚点是猜测**：落地时必须以实际代码为准（tdma_ring_profile.h → tdma_profile.h）
- **G1-G10 预判的缺口**：G1（旧检查器拒收）、G3（性能）、G4（编码）真实发生；G5（逃生门）、G7（Draft 禁止）已实现；其余为流程约定

## 7. 给后续维护的建议

1. 每次新增检查器逻辑 → 同时加 pytest 正反用例 + 跑全量验证
2. 每次新增文档 → 过 docs_check 三道关（元数据/索引/命名）
3. 交叉审核不是可选项：检查器自身也要被审（T12 抓到 4 个真问题）
4. 逃生门审计（`--log-check`）建议每周跑一次

## 8. 域文档三件套约束

当一个域同时维护架构、实施清单和实施证据时，三份标准文件必须分工明确：Architecture 维护稳定
语义，TODO 维护状态和退出门禁，Task Progress 维护提交/构建/HIL/阻塞证据。实施快照不能回填到
Architecture，TODO 只保留证据索引；契约登记仍以 `DOCS_REGISTRY.md` 为唯一事实源。
