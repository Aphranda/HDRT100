# 通用规范本地化工具包

Status: Active
Domain: Standards
Canonical: `docs/standards/STANDARDS_LOCALIZATION_KIT_PLAYBOOK.md`
Related: `docs/standards/STANDARDS_PROJECT_MANAGEMENT_SYSTEM_ARCHITECTURE.md`, `docs/standards/STANDARDS_ADOPTION_GUIDE_PLAYBOOK.md`, `docs/standards/STANDARDS_DOMAIN_ARCHITECTURE.md`, `docs/standards/README.md`
Last updated: 2026-11-06

## 0. 使用说明

这是把通用规范落到具体项目的**填空模板**，不是标准正文。用法：

1. 复制本文件到目标项目（建议放在项目自己的治理目录，如 `LOCALIZATION.md`）；
2. 按 §1–§9 填写；不适用的行写“裁剪 + 理由”，不要留空；
3. 删除示例行或明确标注“示例”；
4. 填完后由项目 owner 和独立复核者各审一次；
5. 通过后，把结论回写到本项目自己的 TODO/进度日志。

原则：**本地化不是复制，而是把通用概念映射到本项目的事实源、owner 和证据。**

## 1. 项目基本信息

| 字段 | 内容 |
|---|---|
| 项目名称 | `<填写>` |
| 项目阶段 | `<方案 / 设计 / 实施 / 验收 / 运维>` |
| 团队规模 | `<人数>` |
| 项目负责人 | `<姓名/角色>` |
| 治理 owner | `<姓名/角色>` |
| 独立复核者 | `<姓名/角色>` |
| 主要交付物 | `<文档/模型/代码/报告>` |
| 法规/合同约束 | `<适用规范、合同条款、签章要求>` |
| 采用范围 | `<全项目 / 单域 / 单流程>` |

## 2. 六系统映射

| 通用系统 | 本项目落点 | Owner | 证据 | 缺口/裁剪 |
|---|---|---|---|---|
| 文档体系 | `<目录/索引>` | `<owner>` | `<入口文档>` | `<缺口>` |
| 契约体系 | `<登记表/接口文档>` | `<owner>` | `<契约 ID/会签>` | `<缺口>` |
| 工具体系 | `<脚本/平台/插件>` | `<owner>` | `<版本/运行日志>` | `<缺口>` |
| Skill 体系 | `<AI 助手/提示库>` | `<owner>` | `<触发词/使用记录>` | `<缺口>` |
| 工作流体系 | `<设计/计算/校审流程>` | `<owner>` | `<WorkflowSpec/回执>` | `<缺口>` |
| 证据与验收 | `<凭证/校审/签章>` | `<owner>` | `<证据目录>` | `<缺口>` |

## 3. 文档三件套

| 文件 | 本项目路径 | 状态 | 备注 |
|---|---|---|---|
| Architecture | `<path>` | `<Draft/Active>` | 稳定语义、边界、owner |
| TODO | `<path>` | `<Draft/Active>` | 任务状态唯一落点 |
| Task Progress | `<path>` | `<Draft/Active>` | 证据、失败、回退 |

元数据模板：

```markdown
# <标题>

Status: Active
Domain: <域>
Canonical: `<path>`
Related: `<path1>`, `<path2>`
Last updated: YYYY-MM-DD
```

## 4. 契约登记

字段：

| 字段 | 内容 |
|---|---|
| contract_id | `<DOMAIN>-<TOPIC>-NN` |
| 承诺 | `<一句话，可判定>` |
| clause_loc | `<文档路径/锚点>` |
| code_anchor | `<代码/模型/文件>` |
| check | `<如何验证>` |
| status | `<pending/active/superseded>` |

首批候选（只登记跨边界、稳定、可验证的承诺）：

| contract_id | 承诺 | 验证方式 | 状态 |
|---|---|---|---|
| `<IF-01>` | `<接口/参数/容量/时序>` | `<命令/用例/会签>` | pending |
| `<IF-02>` | `<...>` | `<...>` | pending |
| `<IF-03>` | `<...>` | `<...>` | pending |

## 5. 工具登记与判型

| 工具/能力 | 形态 | 入口 | Owner | 证据根 | 生命周期 |
|---|---|---|---|---|---|
| `<工具 A>` | `<WORKFLOW/STEP/STANDALONE/LIBRARY>` | `<path>` | `<owner>` | `<out/...>` | `<EXPERIMENTAL/SUPPORTED>` |
| `<工具 B>` | `<...>` | `<...>` | `<...>` | `<...>` | `<...>` |

判型问题：

1. 是否多步、可能等待/恢复？
2. 是否被某个工作流调用？
3. 是否单次命令、单一目的？
4. 是否只被 import 复用？

任一项命中，按对应形态登记；不要“先用起来再补”。

## 6. Skill 清单

| Skill | 触发场景 | 职责 | 不覆盖 | Owner |
|---|---|---|---|---|
| `<project-entry>` | 任何项目工作 | 入口/路由/车道 | 具体业务规则 | `<owner>` |
| `<domain-check>` | 本域校审 | 检查清单/判据 | 签章责任 | `<owner>` |
| `<milestone>` | 长期任务汇报 | 0–6 段模板 | 技术判断 | `<owner>` |

## 7. WorkflowSpec 模板

```yaml
id: wf.<project>.<flow>
version: 1
inputs:
  <input>: <type>
preconditions:
  - <前置状态/版本/许可>
steps:
  - id: s1
    type: query
    action: <只读检查>
    expect: <期望>
    timeout_s: 30
    retry: 0
    evidence: out/<task>/s1.json
    on_failure: abort
  - id: s2
    type: command
    action: <执行>
    expect: <期望>
    timeout_s: 600
    retry: 0
    evidence: out/<task>/s2.log
    on_failure: compensate
  - id: s3
    type: manual
    action: <人工确认/签审>
    expect: <签审完成>
    evidence: out/<task>/review.json
    on_failure: abort
transport: <offline / single_port / multi_port>
receipt_schema: <project>.<flow>.v1
```

## 8. 验收凭证映射

| 改动类别 | 门禁 | 证据 | 台架 |
|---|---|---|---|
| 文档 | 元数据/索引/命名 | 检查器输出 | 否 |
| 工具/测试 | host 测试 | 测试报告 + 指纹 | 否 |
| 固件/硬件行为 | 硬件验收 | P3/HIL 凭证 + scope | **是** |
| 模型/计算书 | 校审 | 模型哈希 + 计算书 + 校审单 | 否（按项目） |

## 9. 90 天里程碑（按项目改写）

| 阶段 | 里程碑 | 退出判据 | 台架 |
|---|---|---|---|
| 第 1–2 周 | 基线与试点 | owner/试点/指标明确 | 否 |
| 第 3–4 周 | 索引与文档门禁 | 新文档 100% 过门禁 | 否 |
| 第 5–6 周 | 契约与工具登记 | 3–5 条契约、Top-10 工具登记 | 否 |
| 第 7–8 周 | 一条工作流 | 可中断恢复、有 receipt | 视流程 |
| 第 9–10 周 | 独立复核 | 非作者复核记录 | 否 |
| 第 11–12 周 | 复盘与推广 | 指标、裁剪、下一季目标 | 视目标 |

## 10. 结构工程示例（已填，供参考）

| 通用系统 | 结构专业落点 | 证据 |
|---|---|---|
| 文档体系 | 设计说明、计算书、校审记录 | 版本号、签审记录 |
| 契约体系 | 设计输入条件、荷载组合、计算假定、专业接口 | 条件表、会签单 |
| 工具体系 | 计算脚本、参数化建模、模型检查器、图纸比对 | 版本、运行日志、验证算例 |
| Skill 体系 | 规范检索、模型检查、审查意见提取 | 触发词、检查清单 |
| 工作流体系 | 方案→建模→分析→验算→出图→校对→审核 | 模型哈希、计算书、校审单 |
| 证据与验收 | 计算书 + 模型 + 校审记录 + 专业会签 | 可复算包 |

示例契约：

| contract_id | 承诺 | 验证方式 | 状态 |
|---|---|---|---|
| `STRUCT-IF-01` | 结构与机电接口坐标统一采用项目基准点，偏差 ≤ 2 mm | 基准点复核 + 专业会签 | pending |
| `STRUCT-CALC-01` | 计算假定与规范版本一致，荷载组合可追溯 | 计算书审查 + 版本比对 | pending |
| `STRUCT-MODEL-01` | 模型、计算书、图纸三者版本一致 | 哈希/版本比对 | pending |

> 示例数字仅用于说明格式，采用时按项目规范替换。

## 11. 评审与裁剪记录

| 日期 | 评审人 | 对象 | 结论 | 必改项 | 复审 |
|---|---|---|---|---|---|
| `<YYYY-MM-DD>` | `<name>` | `<文件/条款>` | `<APPROVE/PASS_WITH_CHANGES/REJECT>` | `<...>` | `<状态>` |

裁剪记录：

| 条款/模板 | 裁剪方式 | 理由 | 影响 | 复验判据 |
|---|---|---|---|---|
| `<...>` | `<删除/简化/延后>` | `<...>` | `<...>` | `<...>` |

## 12. 采用结论

- [ ] 六系统都有落点和 owner；
- [ ] 三件套互相引用，状态/证据不混写；
- [ ] 契约有 ID、锚点、验证方式；
- [ ] 工具有判型和生命周期；
- [ ] 至少一条工作流可恢复、有 receipt；
- [ ] 凭证按改动类别分流，未用 host 冒充硬件；
- [ ] 有独立复核记录；
- [ ] 有明确的裁剪项和下一季目标；
- [ ] 未验证项没有被写成“完成”。

结论：`<可复制 / 需修改后复制 / 不适用>`  
证据：`<路径/记录>`  
下一步：`<...>`
