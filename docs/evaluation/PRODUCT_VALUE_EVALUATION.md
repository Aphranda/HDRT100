# Distributed Hard Real-Time Trigger System 项目价值评估（开源 / 商业 / 学术）

Status: Active
Domain: EVALUATION
Canonical: `docs/evaluation/PRODUCT_VALUE_EVALUATION.md`
Related: `docs/evaluation/README.md`, `docs/evaluation/PRODUCT_VALUE_TRACKING_TODO.md`, `docs/arch/HAOFV_PORTABILITY_EVALUATION.md`, `docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md`, `docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md`, `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md`
Last updated: 2026-09-05

> 本文是项目级**综合价值评估** canonical，位于评估与监督子域 `docs/evaluation/`，作为长期
> 审核监督项目滚动更新（域规则与更新触发见 `docs/evaluation/README.md`；行动追踪见
> `docs/evaluation/PRODUCT_VALUE_TRACKING_TODO.md`）。它不是冻结契约，不登记
> `docs/check/DOCS_REGISTRY.md`（规则见 `docs/check/DOCS_REGRESSION_PLAN.md` C7：评审快照不得登记）。
> 初始评估基线：`refactor/tdma-phy-split-p3-gated` @ `1819fd59df7f28a0aa06616dc8682e29b89e0c75`
> （2026-09-05，远端已同步）。全文采用 **上游评估(R) / 证据(E) → 判断(J) → 结论(C) → 行动(A)**
> 单链结构：判断必须引用上游评估或证据，结论必须引用判断，行动必须说明解除哪条判断的缺口。
> 统计数字为评估当日快照；架构语义以 `docs/arch/HAOFV_ARCHITECTURE.md` 与各域 canonical 为准。

## 0. 评估族谱：本文档与既有文档的关系

架构域（及关联 check 域）已存在多份文档。它们分两类，本文档的处理方式不同：

- **独立 canonical 主文档**（不可合并、独立维护）：`docs/arch/ARCH_PRODUCT_ARCHITECTURE.md`
  （产品化系统架构总纲）、`docs/arch/HAOFV_ARCHITECTURE.md`（顶层架构）及各域 canonical。
  产品架构总纲是"当前主线"的独立文档，定义产品角色/双核模型/发布门禁；本文档**只在其
  产品边界定义之上做价值判断（作为背景 R4），不把它纳入评估合并，不修改它**。
- **评估/规划/审查类快照**（本文档的合并对象）：下述 R1–R3/R5/R6。本文档把它们当作上游
  结论消费，不做重复评估；新增或更新专项评估结论时回到对应文档。

| 上游文档 | 类型 | 回答的问题（专项） | 提供本评估的输入 |
|---|---|---|---|
| `docs/arch/HAOFV_PORTABILITY_EVALUATION.md` | 评估快照 | 代码跨平台可迁移性如何？ | R1 迁移性/耦合分布（上层 9/10、PIO 1/10、整体 6/10→7.5/10） |
| `docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md` | 评估快照 | 顶层架构有哪些 S0–S3 风险？ | R2 风险清单与处置（S0 XIP/Flash、S1 ECC 失真/跨核契约等） |
| `docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md` | 规划（Draft） | 当前产品之后的平台化/开源路线？ | R3 平台化分层、平台映射、F0–F4 开源交付清单 |
| `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md` | 审查快照 | 各域完成度与主链路状态？ | R5 域完成度快照+主链路收敛顺序+统一 active gate 未过 |
| `docs/check/DOCS_REGISTRY.md` + `docs/check/DOCS_REGRESSION_PLAN.md` | 治理事实源 | 契约与治理是否健全？ | R6 契约 27 条、三环门禁、C11（工程治理成熟度） |
| `docs/arch/ARCH_PRODUCT_ARCHITECTURE.md` | **独立 canonical 主文档** | 产品化边界与门禁 | R4 仅作价值判断背景；不合并、不修改 |

### 0.1 回答的核心问题

1. **现在能不能开源？**（还不能 → 差许可/开发证据清理/社区工程三件事；§2.1 J1–J4）
2. **这套东西值不值钱？**（值 → 值在平台 IP 而非单板；卡在最后一公里；§2.2 J5–J8）
3. **能不能变成学术成果？**（能 → 但需要第三方复现与 IP 放行；§2.3 J9–J11）

---

## 1. 事实层：上游评估（R）与一手证据（E）

> R 是既有评估文档的结论（只引用、不重述过程），E 是本评估直接核实的一手观察。
> 编号供 §2 判断引用。

### 1.1 上游评估结论（R1–R6，来自 §0 族谱）

| ID | 上游结论摘要 | 出处 |
|---|---|---|
| R1 | 迁移性两极分化：管理域（事件/仲裁/诊断/FB）9–10/10 零依赖可复用；硬实时 PIO/DMA 路径 1/10 无等价替代（3 PIO block/12 SM）；整体 6/10，修 P0/P1 后 7.5/10（约 4 周）；portable_ota 9/10（14 个 platform ops） | `docs/arch/HAOFV_PORTABILITY_EVALUATION.md` 评分表 |
| R2 | 风险：S0=004 XIP+Flash 双核冲突（已升顶层硬约束）；S1=ECC 190 条失真（文档 58→实际 190）、Vector 缺逐字段标注、跨核契约未落地、Metadata failsafe、49 天回绕；S2=资源优先级/OTA 矩阵/预算 overrun；处置顺序 004→003→001 | `docs/arch/HAOFV_ARCHITECTURE_RISK_EVALUATION.md` 登记表+推荐顺序 |
| R3 | 平台化分层（产品→平台→生态）；八平台映射；四层 portable 边界；开源组件优先交付 simulator/validator/visualizer；F0 当前闭环 → F4 开源生态包；核心卖点"小 MCU 可观察、可校准、可恢复的分布式硬实时系统" | `docs/arch/ARCH_FUTURE_APPLICATION_PLAN.md` |
| R4 | 产品边界（背景引用）：A0 扫描/DPLL 主控、A1 DUT、A2 馈源/极化、A3 VNA/上位机网关；百 ns 级目标（稳定 <10 ns 需 FPGA/TDC）；SCPI/System Pack 只表达意图；发布门禁 9 项。此结论属独立 canonical `docs/arch/ARCH_PRODUCT_ARCHITECTURE.md`，仅背景引用 | `docs/arch/ARCH_PRODUCT_ARCHITECTURE.md`（独立 canonical，不在合并范围） |
| R5 | 域完成度快照：OTA v1≈86%、RefMem≈70%、SD≈63%、TDMA≈50%、VDC≈46%、Flash v2≈45%、RTOS/HAOFV≈39%、Calibration≈39%、RS485≈17%；主链路=硬件时间戳→Calibration active→TDMA 常驻环→VDC/DPLL LOCKED→RefMem 可靠完成→T2→长稳，统一 active gate 未过 | `docs/check/DOCS_PROJECT_STATUS_REVIEW_20260824.md` |
| R6 | 治理三环门禁（7 天新鲜度环 + 契约登记 + pre-commit 双检查器）；登记 27 条契约（5 active/21 pending/1 superseded）；C11 禁止自审自批；另有 P3 硬件验收凭证门禁 | `docs/check/DOCS_REGISTRY.md`、`docs/check/DOCS_REGRESSION_PLAN.md`、`.githooks/pre-commit` |

### 1.2 一手证据（E1–E12，2026-09-05 观察）

| ID | 证据 | 来源 |
|---|---|---|
| E1 | 仓库公开于 GitHub，`license: null`，0 star / 0 fork，无 topics/描述/Releases | GitHub REST API `repos/Aphranda/HDRT100` |
| E2 | 根目录无 LICENSE/CONTRIBUTING/CHANGELOG/SECURITY；许可证仅存在于 third_party 上游 | 根目录清单、`third_party/README.md` |
| E3 | 934 commits（2026-06-21→09-05，8 月 809 条）；提交邮箱域名 generaltest.com；板端厂商串 `GTS,DHRT100` | git log/shortlog；构建与文档 |
| E4 | 内部进度文档含板卡唯一序列号、真实 COM 口与台架拓扑 | 各域 `*_TASK_PROGRESS.md` |
| E5 | 代码规模：跟踪 1137 文件；自有固件 C/H≈10.5 万行、自有 C≈17.7 万行（剔除 u8g2 字体 45.3 万行）、Python≈7.4 万行、MD≈5.4 万行；tools≈90 个；tests=81 pytest+约 63 host C+HIL | git ls-files+行数盘点（评估快照） |
| E6 | README 提供独立构建（CMake presets+`pico_sdk_import.cmake`，SDK 2.2.0/toolchain 14_2_Rel1 钉死）；pico-sdk 未内置、未钉 tag；HIL 类验证依赖五块真实板卡 | 根 `README.md`、`CMakeLists.txt`、`pytest.ini` |
| E7 | 同步精度资产：PIO 4 ns 量化 bin；时间戳门禁 ≤100 ns；目标 e_vdc P99≤100 ns/T2 error P99.9≤300 ns；P3 实测单链路 delay 78–82 ns、CLK 整环 RTT 400–500 ns（perf 数字为 HIL/诊断快照） | `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`、`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`、`docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md` |
| E8 | 当前重构分支主链路未收敛到产品级：NO1–NO4 仍 CHECKING、Calibration endpoint bias 被硬件环回阻塞（P3-03 BLOCKED）、DPLL P1 板端 OTA 曾现 COM3 回滚/COM25 INVALID_STATE。**注意：锁相能力并非未验证**——2026-08-16 两板 COM5/COM6 GPIO overlay HIL 已实测 `state=LOCKED`（build `20260816132834`/`20260816134351`/`20260816175137`，`source=HARDWARE_TICK`、resolution 100/1000 ns、`flags=DPLL_ELIGIBLE`、`gate=PASS`）；当前 CHECKING 是 TDMA/四板化大改后未收敛，不是能力缺失 | 各域 TODO/Task Progress + `docs/vdc/VDC_TASK_PROGRESS.md`（2026-08-16 HIL 记录，快照） |
| E13 | 作者澄清（2026-09-05，随本评估记录）：① 平台核心（HAOFV/TDMA/RefMem/VDC/System Pack 等基础件）已基本与客户产品、公司产品分离，后续将完全独立；② 锁相能力此前在主分支/早期基线上已验证，当前阻塞仅是大改动收敛问题。仓库侧佐证：代码与 docs 未检索到客户/公司产品专属内容（E10），仅有厂商串与板级开发证据 | 项目所有者澄清 + 本评估会话记录；佐证见 E10 |
| E9 | 产品化配套未冻结：产品板硬件约束 Draft、AMC1301 高侧采样首件过热属硬件阻塞、USB VID/PID 与 100 mA 供电描述符待替换 | `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`、`docs/interface/SCPI_USB_INTERFACE_DESIGN.md` |
| E10 | 全库检索不到 NDA/保密/专利/论文字样 | docs 关键词检索 |
| E11 | **PIO 程序按 persona 动态装载（不全部常驻）**：PIO 指令内存按功能装载/卸载，persona 枚举事实源为 `tdma_pio_spi_program_persona_t`（NORMAL、粗 CLK 训练、板内校准回环、编码 CLK 等），装载由 core1 owner 在 SM/DMA stop + safe IO gate 后执行，失败恢复上一 persona 并保持 STOPPED；SYNC_IO 侧另有 INPUT_CAPTURE/WAVE_OUTPUT/SCHEDULED_TRIGGER/LOGIC_ANALYZER/SMA 等 persona 生命周期（claim→load→ARM→RUN→release）。PIO program 随签名 App catalog 发布、System Pack 只选已登记 program/persona ID（ARCH-PIOCAT-01），即可随 OTA 更新 IO 语义，不加载任意机器码 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`（PIO 动态装载段）、`docs/sync/SYNC_IO_ARCHITECTURE.md`（persona）、`docs/arch/HAOFV_FLASH_ARCHITECTURE.md`（ARCH-PIOCAT-01）、`docs/calibration/CALIBRATION_TASK_PROGRESS.md`（CAL-TASK-20260821-004） |
| E12 | **TDMA 时隙/维护窗口编排功能切换与训练**：TDMA 定义 window class/guard/deadline 与 maintenance gate（默认关闭，只有显式 maintenance window 才打开，配置/OTA/LOG 属低优先级 maintenance traffic 且不得借用实时 guard）；训练执行顺序固定 `STOP→APPLY→ARM→TRAIN→publish/restore→STOP`，reference 发 `TRAIN_PREPARE`、收齐 ACK 后 commit sequence 统一切换全节点 training persona，结束恢复普通 persona——即 persona 装载/校准/维护都在确定性时隙边界内完成，不打断实时路径 | `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`（schedule/window/maintenance gate/训练编排段）、`docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md` |

---

## 2. 判断层：每个维度的综合解读（J）

### 2.1 开源潜力（Q1）

| ID | 判断 | 引用 | 关键点 |
|---|---|---|---|
| J1 | **现状不能合法开源**：无 LICENSE = 默认保留所有权利 | E1+E2 | 第一阻塞不是代码而是许可。 |
| J2 | **泄密风险大幅收窄但未归零**：仓库含厂商串（`GTS,DHRT100`、generaltest.com）、板卡序列号与台架拓扑；但 E13 表明平台核心已与客户/公司产品分离且将完全独立，故无客户产品机密残留，剩余为厂商身份与开发证据，脱敏成本低 | E3+E4+E13 | 分层开源（基础件公开、厂商/序列号/台架清理）后即可开源，非长期阻塞。 |
| J3 | **治理与复现达开源水准，但参与门槛高**：门禁严苛 + HIL 依赖五块真实板卡 | R6+E5+E6 | 治理是差异化优点，也是社区门槛。 |
| J4 | **题材稀缺且有现成路线**：R3 已给 F0–F4 与 simulator/validator/visualizer 交付清单；R1 说明管理域可复用、PIO 路径为特色而非阻碍；E11/E12 的"运行时 persona 装载 + 时隙门控"本身是低成本可重构确定性 IO 的教学题材 | R1+R3+E5+E11+E12 | 开源后易形成"小而准"生态位。 |

**小结**：内容与治理够格（J3/J4），阻塞全在许可/开发证据清理/社区工程（J1/J2）——
**现状 5/10 → 完成 A1–A3 后 8/10**。

### 2.2 商业价值（Q2）

| ID | 判断 | 引用 | 关键点 |
|---|---|---|---|
| J5 | **价值主张真实，锁相能力已有验证、但重构分支未收敛复现**：百 ns 同步硬件实现能力已由 2026-08-16 两板 LOCKED HIL 佐证（E8），主链路收敛目标是"TDMA 常驻环 + 四板复现 LOCKED + T2 + 长稳"，当前差在重构收敛与产品级证据 | R4+R5+E7+E8+E13 | 卖点成立前提是重构分支复现四板 LOCKED+T2+长稳，而非从零验证。 |
| J6 | **成本替代叙事清晰且含"对 FPGA 的差异化优点"**：1–2 美元 MCU+PIO 对标 EtherCAT DC/PXI/FPGA 非极限档；与 FPGA 相比，换功能不必重新综合/下载 bitstream——E11 的 PIO persona 动态装载（随签名固件 OTA 更新 IO 语义）在 E12 的 TDMA 确定性时隙/maintenance gate 内装载与切换，不打断实时路径 | R4+E7+E11+E12 | 对"多通道、非极限精度 + 多模式现场升级"有真实替代空间。 |
| J7 | **平台期权大于单板价值**：R1 证明管理域可复用、R3 给出横向扩展（仪表/DAQ/ATE/运动）；可复用资产含 OTA/SCPI/System Pack/BiSS-C/portable_ota | R1+R3+E5 | 变现应含平台授权/移植服务。 |
| J8 | **产品化配套与工程质量风险可控但未清零**：R2 的 S0/S1 已多数承接但仍有多项未落地（如跨核契约、ECC 默认规则化）；E9 硬件/枚举配套未冻结 | R2+R4+R6+E8+E9 | 商业化前需工程收尾+第三方复现+认证。 |

**小结**：壁垒与市场真实（J6/J7），风险在"最后一公里+产品化配套"（J5/J8）——
**现状 6/10 → 随 A4–A5 闭环上升**。

### 2.3 学术价值（Q3）

| ID | 判断 | 引用 | 关键点 |
|---|---|---|---|
| J9 | **实证题材可投稿但证据为工程自证**：纯 MCU 百 ns 级分布式同步数据稀缺；无第三方复现、无同条件对照（PTP/EtherCAT DC）、无误差分解 | R5+E7+E8 | 需对照实验与误差建模才能升格。 |
| J10 | **架构取舍有方法学论文素材**：IEC 61499 风格 FB+Active Object+"RefMem 数据面替代分布式运行时"的取舍在 R2/R6 中有完整论证与记录；E11/E12 的"persona 装载 + 时隙门控"可作"低成本动态重配置替代 FPGA PR/bitstream"的实证比较 | R2+R6+E11+E12+`docs/arch/HAOFV_ARCHITECTURE.md` | 素材密度够，缺学术化重写。 |
| J11 | **无 IP 放行与学术痕迹**：无专利/论文声明；文档中文、术语内部化 | E3+E10 | 是流程阻塞而非内容阻塞。 |

**小结**：方法与数据素材真实（J9/J10），短板全在"独立验证+IP 放行"（J9/J11）——
**现状 4–5/10 → 补 A6–A7 后 7/10**。

---

## 3. 结论层（C）

### 3.1 评分标准

| 分数 | 含义 |
|---|---|
| 1–3 | 有碎片资产，无成型价值主张 |
| 4–6 | 价值主张成型，但存在可枚举的决定性缺口 |
| 7–8 | 决定性缺口解除，进入放大阶段 |
| 9–10 | 生态/营收/引用形成正循环 |

### 3.2 三维汇总

| 维度 | 现状 | 可达 | 决定性缺口 |
|---|---|---:|---:|---|
| 开源潜力 | 5/10 | 8/10 | 无许可证（J1）+ 厂商/开发证据未清理（J2）+ 无社区工程（J3） |
| 商业价值 | 6/10 | 随闭环上升 | 锁相能力已验证但重构分支未收敛复现四板产品级闭环（J5）+ 产品化配套未冻结（J8） |
| 学术价值 | 4–5/10 | 7/10 | 工程自证无第三方复现（J9）+ 无 IP 放行（J11） |

### 3.3 一句话总结论

**C1**：在 R1–R6 的既有评估之上综合判断——本项目的稀缺性 = **治理与文档达开源顶级（R6）
× 平台化架构含真实技术纵深（R1/R3/R4）× 低成本确定性同步 + 运行时 persona 重构题材
（E7/E11/E12）**；其中 E11+E12 构成对 FPGA 的差异化优点：IO 语义可随 OTA 经 persona
动态装载、由 TDMA 确定性时隙门控切换，而非重新综合 bitstream。按作者澄清（E13），平台核心
已与客户/公司产品分离且将完全独立，锁相能力亦已有两板 HIL 佐证（E8）——因此它处于
"重构分支收敛复现产品级闭环、许可与开发证据清理、产品化配套"的收尾关口。**缺的不是东西
本身，而是把它对外主张的三道放行手续：许可、收敛后的闭环证据、IP 授权。**

### 3.4 反向证伪（结论何时不成立）

- ~~若 DPLL/VDC 无法锁相则卖点降级~~：此证伪已排除——E8 显示 2026-08-16 两板 GPIO overlay
  HIL 已实测 `LOCKED`（100/1000 ns、`HARDWARE_TICK`、`DPLL_ELIGIBLE`、gate PASS），E13 也
  确认能力在主分支验证过。真正的风险不是"能否锁相"，而是 **TDMA/四板化重构能否收敛复现
  产品级闭环（四板 LOCKED + FINE_100NS + T2 + 长稳）**；若长期无法收敛，J5/J6 的"可发布"
  主张持续延后 → 由 `TDMA-DPLL-001..008` 与 P3 验收裁决（见 §5 信号 1）。
- 若平台独立化后仍发现客户/产品专属机密无法分层（E13/E4 评估偏差），J2 将重新升级为长期
  阻塞 → 开源停留在 5/10 以下。当前证据不支持该场景，但保留该证伪分支。

---

## 4. 行动层（A）：按投入产出比排序

| ID | 行动 | 解除 | 成本 | 解锁 |
|---|---|---|---|---|
| A1 | 落地 LICENSE（Apache-2.0 或 MIT）+ third_party 许可证清单 + CONTRIBUTING/CHANGELOG/SECURITY + README 英文化 | J1、J3 | 半天 | 开源全部后续（C1 放行一） |
| A2 | 厂商/开发证据清理 + 分层开源：先开 portable_ota/log、校验工具、simulator/validator/visualizer；厂商串、序列号、台架拓扑以"基础件公开 + 开发证据分仓/清理"处理 | J2 | 1–2 天/批 | 生态引流（平台已与客户/公司产品分离，见 E13） |
| A3 | GitHub topics/描述/Releases + 一键复现脚本（hooks 配置、钉死 pico-sdk） | J3 | 1 天 | 外部贡献者可自证，bus-factor>1 |
| A4 | 解 DPLL P1 板端 OTA 回滚（COM3/COM25）与 endpoint bias 硬件环回；在重构分支**复现**四板 LOCKED + T2 + 24 h 长稳（能力已有两板 HIL 佐证，E8） | J5、J8 | 数周（当前主线） | 第一份可对外产品级精度报告（C1 放行二+商业拐点） |
| A5 | 冻结产品板硬件约束+AMC1301 修复+USB VID/PID 产品化 | J8 | 与 A4 并行 | 可售硬件形态 |
| A6 | 第三方/跨平台（STM32H7）精度复现+误差分解（晶振漂移/温度/4 ns 量化），并整理 E8 既有两板 LOCKED HIL 证据链为对外主张 | J5、J9 | 2–4 周 | "78–82 ns/100 ns"可对外主张+论文核心实验（C1 放行三） |
| A7 | IP 审查 + 实证/架构两条线论文与脱敏数据 artifact | J9、J11 | 与 A6 并行 | 学术产出+开源 artifact 复用 |

---

## 5. 复评闭环（何时推翻/更新本评估）

1. `TDMA-DPLL-*` 任一 HIL 完成（重构分支复现四板 LOCKED/holdover/relock）→ 裁决 C1 与 J5 的收敛判断。
2. LICENSE/开发证据清理/社区工程任一项落地 → 刷新 J1–J3 与开源评分。
3. 校准 endpoint bias（P3-03/P3-LB-06）解除 → 刷新 J5/J9 数据主张强度。
4. 出现论文/专利动作或公司 IP 决策 → 刷新 J11。
5. 域完成度快照随 `DOCS_PROJECT_STATUS_REVIEW_*` 更新 → 刷新 R5 与 §3.2。
6. 上游专项评估更新（R1/R2/R3 任一修订）→ 回到对应文档重读后刷新本表。

## 6. 边界与来源

- 本文消费 §0 族谱列出的全部上游文档，但**不修改它们**（专项结论以各自文档为准）；
  本文只更新本文件自身。修改上游评估属于各自的评审流程，且受 C11 交叉审核约束。
- 数字类事实（E1/E3/E5/E7 等）为评估快照，非代码事实源；perf 数字权威来源为各域
  TODO/Task Progress 与 `out/hardware_acceptance/` 验收证据，引用前须回查。
- 本文件遵循文档自回归体系：命名/元数据/索引合规（`docs/README.md`、
  `docs/evaluation/README.md` 已登记本文件）；评审快照不登记契约（C7），故不修改
  `DOCS_REGISTRY.md`。本文作为长期追踪评估载体，追踪规则与行动状态分别见
  `docs/evaluation/README.md` 与 `docs/evaluation/PRODUCT_VALUE_TRACKING_TODO.md`。

## 7. 追踪评估记录（每次复评追加一行）

| 日期 | 触发 | 更新摘要 | 依据证据 |
|---|---|---|---|
| 2026-09-05 | 初始建档 | 在 `refactor/tdma-phy-split-p3-gated`@`1819fd5` 基线建立综合价值评估（R/E/J/C/A 链），并入 E11/E12（PIO persona 动态装载 + TDMA 时隙编排）、E8/E13（锁相能力已有两板 HIL 佐证、平台核心与客户/公司产品分离） | E1–E13、R1–R6、VDC_TASK_PROGRESS 2026-08-16 HIL、作者澄清 |
