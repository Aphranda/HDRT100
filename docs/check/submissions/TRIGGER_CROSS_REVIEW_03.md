# 核验提交单：TRIGGER（T2 一等席运输） → TDMA 域联审

Status: Active
Domain: TRIGGER
Canonical: `docs/check/submissions/TRIGGER_CROSS_REVIEW_03.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`, `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`, `docs/check/DOCS_REGISTRY.md`, `docs/check/submissions/TRIGGER_CROSS_REVIEW_01.md`
Last updated: 2026-10-13

> 提交方: T2 域（作者） → 核验方: **独立 TDMA 域侧联审**（独立 subagent `53220251-c737-4ae6-b355-e4f076f590f3`，
> **未参与** T2 或 TDMA 任一侧起草）；方式: 代码 + 登记表 + 文档**只读交叉核验**（C11）；
> 核验未创建/修改/删除任何文件，未运行构建或硬件工具。
> 结论: **有条件接受（`PASS_WITH_NOTE`）**。
> 关闭的缺口: `T2-AUDIT-03` 的第三项「与 TDMA 一等席布局联审」。

## 提交内容

| # | 问题 | 判定 | 证据（file:line） |
|---|---|---|---|
| ① | 布局：offset/size、越界、对齐、冲突、静态断言 | ⚠️ **尺寸与断言成立；两处表述不准确** | `components/tdma/inc/tdma_process_image_layout.h:47-50,71-72,79-84,86-112`；`components/tdma/inc/tdma_flight_engine.h:18,30-32`；`components/distributed_refmem/src/distributed_refmem.c:33-34,1038-1051,1086-1089` |
| ② | 版本耦合归属（`TDMA-PROCESSIMAGE-01` 是否成立、是否需新增条款） | ⚠️ **归属方向成立，但条款缺失，须新增** | `docs/check/DOCS_REGISTRY.md:32`；`docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:982-1033`；`tdma_process_image_layout.h:13`；`git log -L 13,13:…layout.h`（仅 `55828779` 一次命中）；`git show 595a76d2` |
| ③ | 发布机会与预算：比例真值、1 kHz 下期限、机制真伪 | ⚠️ **机制真、比例真；「编译期常量」假；无期限保证；预算与比例自相矛盾** | `components/tdma/inc/tdma_pio_spi_ring_adapter.h:281-286,291,344-364`；`tdma_pio_spi_ring_adapter.c:543-546`；`tdma_pio_spi_ring_origin.inc:478-481,487-495,500-507,515-527,531-543`；`tdma_first_class.h:50-96`；`tests/python/test_tdma_seat_rotation.py:212-234` |
| ④ | `SYNC_TRIGGER` phase 预算与相位号 | ✅ **完全一致（历史误解析坑已独立复核）** | `application/inc/app_realtime_schedule.h:39-42,52-58`；`config/project_config.h:41,53-62,111-115,139-147`；`boards/rp2350_trig/inc/board_config.h:9`；`middleware/scpi_port/src/scpi_system_snapshot_commands.c:3422-3452`；`application/inc/app.h:19` |
| ⑤ | process image CRC 归属与「两板 effective schedule CRC 一致性门」 | ✅ **符号与机制均真实；归属准确；措辞应指 TDMA 符号** | `tdma_process_image_layout.h:252-271`；`distributed_refmem.c:1086-1089,1309-1310`；`distributed_refmem_position_ack.inc:148,165-166,203-211,286`；`components/tdma/src/tdma_priority_rx.c:178-179,185,189,195-198`；`refmem_vector_table.h:53-54,162-165`；`docs/arch/HAOFV_ARCHITECTURE.md:496` |
| ⑥ | 联审立场 | **有条件接受** | 见「偏差声明」与「未闭合条件」 |

**④ 的内核结论（T2 侧数字获独立重算确认）**：相位 **7**、WCET `21800 + 2×2500` = **26800**、窗口 **[335000, 365500)** 宽 **30500**（@250 MHz 为 107.2 µs / 122 µs）、REFMEM 298500..332000、SCPI 每相位 **11** 字段——与 T2 侧更正后的数字**逐位吻合**。
唯一易混点（非错误）：`quarantined_mask` 是 **LOAD 掩码**（bit 6 = `APP_REALTIME_LOAD_SYNC_TRIGGER`），相位号 7 与 LOAD 位 6 属**两个编号空间**。

**③ 的真实断言（机制不是意向）**：与特等按**发布机会**轮换；特等 `EMPTY` 时不占座、机会让给一等；两级皆空**不消耗机会**且座位保持上一份内容；一等记录等待中不被覆盖、malformed 记录退休不阻塞。**假的部分**：比例自 `f636ebfa`(2026-10-11) 起已改为 **STOP-only 每板设置**（`SYSTem:TDMA:SEAT:ROTation`），1:1 只是编译默认值；`TDMA_SEAT_ROTATION_PRIORITY_SHARE`/`_FIRST_CLASS_SHARE` **全库零命中**（见下）。
**无期限保证**：等待预算是**发布机会数**（8），达到预算**不丢记录、不退休**，只 `seat_first_class_wait_violation_count++`；带时间窗的 `tdma_seat_window_t` **未接线**。量级（快照，非事实源）：Core1 整表周期 375000 cyc = **1.5 ms** @250 MHz，实测快速路径间隔 ≈ **2.13 ms**，1:1 下 ≈4.3 ms，预算 8 次机会 ≈17 ms——**「1 kHz 时间片」是 wire/VDC 节拍，不是每毫秒一个一等座位**。

## 偏差声明

**T2 侧已办（按联审意见更正本域登记行，2026-10-13）**：

1. **错别字兼悬空引用**：`ARCH-TRIGGERSLICE-01` 行原写 `REPMEM-LAYOUT-01` → 更正为 **`REFMEM-LAYOUT-01`**。
   原写法在登记表中**不存在**（真实 id 见 `docs/check/DOCS_REGISTRY.md:22` 与 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:982`），属引用了不存在的契约 id。
2. **归属错误**：原写「前 8 B 为 **core1** 元数据」→ 更正为「前 8 B 为 mailbox **fast header**，由 **Core0** 镜像构建写入（`distributed_refmem.c:984-990`：magic/version/class/source/target/seq16），Core1 仅上车前复验」。
3. **偏移歧义**：原写「该区 offset 14」与「位于 24 B core0 内容体之内」并置，易被误读为 22 → 更正为「**slot 绝对偏移 14**；24 B core0 内容体内的相对偏移为 6」。
4. **版本耦合强度收紧**：原写「尺寸随 TDMA process image 布局版本耦合」会被读成有版本递增保证 → 更正为「**符号级**耦合；`TDMA_PROCESS_IMAGE_LAYOUT_VERSION` 自引入未递增，混版本保护实际靠 **class 白名单拒帧**（普通 transport 白名单不含 0x15）而非版本字段」。

**T2 侧记录但不在本域处理（跨域，仅指针）**：TDMA 侧条件 **C1/C2/C3/C4** 与 arch 侧 **C5**（见下），归各域 TODO，T2 不代管。

## 未闭合条件（跨域，仅登记为指针，不计为 T2 待办项）

| 条件 | 域 | 内容摘要 |
|---|---|---|
| **C1** | TDMA | 新登记 `TDMA-SEAT-01`「一等席容器与预算」：M:N 为 STOP-only 每板设置（默认 1:1、每 share 1..8、`M+N ≤ 12`）；有新内容才消耗机会、两级皆空不消耗且座位保持上一份内容；一等记录单深度单写者、等待中不被覆盖；预算以**发布机会数**声明、达到即计一次越界但**不丢记录**；只承诺机会公平性与有界可计数等待，**不承诺时间期限**；class 0x15 **整段**复用 10 B RefMem 子区、一帧发完不分片、解析器先看 class 字节。载体建议 `docs/tdma/TDMA_RUNTIME_CONSTRAINTS.md`（**不得**用自述为评审快照的 `TDMA_SEAT_ACCEPTANCE_REVIEW.md`——AGENTS.md 硬约束 6） |
| **C2** | TDMA | `TDMA-PROCESSIMAGE-01` ver 1→2：追加「RefMem 子区按 class 复用；子区尺寸或 class 语义变更**必须递增** `TDMA_PROCESS_IMAGE_LAYOUT_VERSION`；旧固件按 class 白名单拒收 0x15（不误解析）」 |
| **C3** | TDMA | 预算与比例一致性：`SHARE_MAX == WAIT_MAX == 8` 使合规 8:1 **按设计**置位 `SEAT:HEALth?` bit3；择一修正（预算改 9 / 仅在 `wait > 预算` 时计数 / `SHARE_MAX ≤ 7`）。同时关闭 `docs/tdma/TDMA_DOMAIN_TODO.md` 中自述未履行的登记义务 |
| **C4** | TDMA | 期限口径：TDMA 不声明时间期限，一等席期限预算**保持未闭合**，待 T2 冻结正式时限（`TRIGGER_T2_ARCHITECTURE.md` 与 Task Progress 的「正式时限」项现状未冻结）后复核 |
| **C5** | arch | `docs/arch/ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md`：`:57` 引用**已删除**的符号 `TDMA_SEAT_ROTATION_*_SHARE` 且称「编译期常量」（实为 STOP-only 可配置、默认 1:1）→ 属 AGENTS.md 硬约束 2 违规；`:27`/`:135`/`:151`/`:163` 仍在描述已作废的「T2 片段占用普通帧 RefMem 区／与 VDC 同帧」模型，与同文件 `:57` 及 TDMA canonical 冲突 |

**登记顺序建议（TDMA 侧）**：C1+C2 同批（契约与版本语义耦合，分开登记会留下中间不一致态）。登记前注意 `docs/tdma/TDMA_TASK_PROGRESS.md` 已超 C14 上限（登记轮转债 2026-10-17）。

## 零命中清单（治理缺口，据实记录）

| 被引用的对象 | 结果 | 搜索范围 |
|---|---|---|
| `TDMA_SEAT_ROTATION_PRIORITY_SHARE` / `TDMA_SEAT_ROTATION_FIRST_CLASS_SHARE` | **零命中**（`ARCH_REALTIME_LOAD_MOUNTING_CONSTRAINTS.md:57` 以「当前轮换比例」引用） | 全库；限 `*.{c,h,inc}` 仍零 |
| `TDMA-SEAT-01/02/03` 的**登记行** | **零命中**：`DOCS_REGISTRY.md` 无任何 SEAT 行 | `SEAT\|seat\|EXE-` on `docs/check/DOCS_REGISTRY.md` |
| `EXE-SEAT-01` 的定义文档／登记／结论 | **零命中**（被 T2 与 TDMA 共 5 处当作前置引用） | `EXE-SEAT-01` 全库 |
| 一等席**时间期限**符号／条款 | **零命中**（`tdma_seat_window_t` 存在但未接线） | `deadline\|TDMA_SEAT_BUDGET\|start_ns\|close_guard`、`TDMA_SEAT_*` |
| 「RefMem 子区按 class 复用 + 版本递增」的**成文条款** | **零命中** | `git log -L` on `TDMA_PROCESS_IMAGE_LAYOUT_VERSION`；登记行文字 |
| Core1 写 mailbox fast header 的证据 | **反向零命中**：`tdma_pio_spi_ring_origin_board()` 原样发布，不写任何 header 字节 | `tdma_pio_spi_ring_origin.inc:395-416` |

**对 T2 的直接后果**：`T2-IDENT-001` 中「与 TDMA 一等席布局一起核定并按 `EXE-SEAT-01` 登记（C11）」这一步**目前无落点**（该 id 无定义、无登记、无结论记录），须先由相应域建立 `EXE-SEAT-01` 的载体，或改指到 C1 登记的 `TDMA-SEAT-01`。

## Alternatives considered

- **方案A** 据本次联审把 `ARCH-TRIGGERSLICE-01` 置 `active`（拒绝：联审结论为**有条件接受**，且其引用的 `TDMA-PROCESSIMAGE-01` 尚需按 C2 修订、`TDMA-SEAT-01` 尚需按 C1 登记；此时置 active 等于把一个**符号级**耦合声明成有版本保证）。
- **方案B** 由 T2 域代为登记 TDMA 侧 C1/C2 条款（拒绝：跨域越权，且本联审方已声明不宜兼任其 C11 审核方，须另派审核方）。
- **方案C** 只出结论、不更正 T2 侧登记行（拒绝：该行含**指向不存在契约 id** 的引用与两处归属/偏移错误，不修会持续误导下游）。

## 核验结论

- 结论: `ACCEPT_WITH_DEVIATION`（对应联审方 `PASS_WITH_NOTE` / 有条件接受；T2 侧三项表述与一处引用缺陷已修正，四项跨域条件登记为指针）
- 核验人: T2 域（作者侧；**仅按联审意见实施本域修正**，不参与本次联审判定）

## 交叉审核记录（C11，必填）

- 审核方: 独立 TDMA 域侧联审 agent（独立 subagent `53220251-c737-4ae6-b355-e4f076f590f3`），**未参与** T2 或 TDMA 任一侧起草
- 审核方式: agent 交叉／代码 + 登记表 + 文档**只读**交叉核验（`git grep`、`git log -L`、读文件、两个只读文档检查器）；未创建、修改、删除任何文件；未跑构建、未跑 `tools/hardware_acceptance/*`
- 审核结论: `PASS_WITH_NOTE`（= 有条件接受；C1–C4 契约级、C5 文档级）
- 审核日期: 2026-10-13

## 未核验范围（据实记录）

1. **所有实测数字**（2.13 ms 快速路径间隔、四板 T2 送达/冻结、DPLL60 等）均为**文档快照**，非事实源；只核了其与编译期常量是否自洽。
2. **真机 SCPI 读回**：无设备；相位窗口与 11 字段为静态重算 + 处理器源码计数。
3. **未编译、未跑 host 测试**：「M=8:N=1 越界」为**读测试源码**所得，非运行结果。
4. **VDC 域 effective schedule CRC 门**：只核到符号、composition helper 与 TDMA lane 比较点，未端到端审 VDC 准入路径。
5. **「只有参考节点 staging 0x15」的拓扑不变量**未在代码层证明（角色门在 T2 运行期而非 staging 内）。
6. **源码指纹一致性**未核（工作树与 T2 侧证据快照是否同指纹）。
