# T2 预约触发实施待办

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TODO.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
Last updated: 2026-09-22

## 文档接口与状态规则

本文是 T2 实施任务状态唯一落点；[Architecture](TRIGGER_T2_ARCHITECTURE.md) 维护语义，[Task Progress](TRIGGER_T2_TASK_PROGRESS.md) 维护执行证据。状态仅用 PENDING、IN PROGRESS、BLOCKED、DONE；DONE 只关闭对应任务，不自动提升父任务、物理精度或产品资格。

## 已有基线与当前主线

已具备四板 TDMA/DPLL、TIMER1 映射、诊断输出和示波器联合观测基线；这些不证明预约部署、fence 或 T2 actual 已实现。用户确认借鉴 node-sequence 的单节点多槽能力，当前顺序是：**先多板实际槽位部署，再开发 T2 预约触发。**

参考分支有槽位/角色校验、序列运行以及预约配置/时钟/规划模块；跨板预约运输、fence、硬件 deadline 和 completion 仍需本分支实现验收。来源 commit、实际存在文件和单板证据范围见进度，不整分支合并治理、USB、OTA、GUI 或单板门禁。

## 里程碑

首轮部署由用户确认（部署目标，非硬件事实源）：NO1=COUNTER、NO2=SP8T、NO3=VNA 网分网关、NO4=SP2T/HV。板卡身份可动态切换，不能把角色编译绑定在板号中；实际 UID、TDMA slot、输出和反馈引脚仍须现场读回及接线确认。

网关必须同时处理 TRIG 与 REDY；每个 T2 节点都有本地反馈配置、等待/超时/恢复和结果回传。REDY 的电平/边沿语义及其他角色的物理反馈能力待确认，不默认填成完成。

OUT1 外部周期保持，周期内部按可配置细分格建立 T2 时间基准；各板共享 VDC anchor/周期/细分身份，T2 目标和反馈窗口引用绝对细分序号。内部细分不要求输出更多脉冲；细分数、最小支持间隔和量化界待实现验证，不把格间隔当物理精度。

| 阶段 | 可交付结果 | 进入下一阶段的依据 |
|---|---|---|
| M0 多板部署 | 逻辑槽位绑定真实板卡，各板仅执行本板角色，身份可切换，命令与回执跨板闭合 | 当前 build 的配置读回、路由、实际动作、角色互换和停止恢复通过 |
| M1 本地预约 | 指定未来 VDC 目标可转换并提前装载，在本板硬件执行 | 身份/模型/lead/资源负测和物理输出证据通过 |
| M2 多板预约 | PREPARE/READY/fence 驱动目标板按共同未来时刻执行 | 同预约多板输出和异常不误触发证据通过 |
| M3 T2 结果闭合 | 输出或设备完成实际 latch 经绑定模型映射并回传 | 实际回读、completion、超时/失效和明确质量范围通过 |

## 分阶段任务

| ID | 任务 | 状态 | 完成或退出门禁 |
|---|---|---|---|
| T2-DOC-001 | 建立三件套与导航 | DONE | 分离设计/任务/事实；总索引、独立复核和文档门禁通过，见 T2-PROGRESS-20260922-001；不表示 T2 功能完成 |
| T2-DEPLOY-001 | 确认活动槽位与多板映射 | DONE | 用户确认当前映射：NO1 COUNTER/IN1；NO2 SWITCH_A/SP8T/OUT1–3；NO3 GATEWAY/VNA OUT4+READY IN1；NO4 SWITCH_B/SP2T/HV OUT1。NO2/NO4 抽象为同一 SWITCH 类型，设备差异由 mask/profile 表达；READY 极性仍待设备协议确认，见 T2-PROGRESS-20260922-006 |
| T2-DEPLOY-002 | 迁入最小部署模型及配置事务 | DONE | 完整表 canonical CRC、STOP STAGE/读回/ACTIVATE、UID/TDMA slot/IO/反馈路由及代际高水位已实现；四板原映射、两 SWITCH 身份交换、恢复及错误CRC/错目标/旧代负测通过，见 T2-PROGRESS-20260922-006。active 仅为路由映射，不授予 IO claim/ARM |
| T2-DEPLOY-003 | 多板槽位业务闭环 | PENDING | 依赖 T2-DEPLOY-002；真实 TDMA 分发、来源与目标映射、各板动作、具身份回执、STOP 与重启后的旧消息隔离；本地回环/注入 ACK 不能替代跨板证据 |
| T2-DEPLOY-004 | 同固件动态身份切换 | PENDING | 在板卡间交换角色并恢复；停止新预约、处理在飞动作、输出/反馈捕获退休、旧资源释放、新配置读回及部署代发布可追溯；旧 READY/REDY/completion 不授权新角色；切换失败不留下部分活动部署 |
| T2-CLOCK-PROBE-001 | OUT1 有限细分脉冲间隔实测 | DONE | STOP 下宽标记和可配置窄脉冲完成四板指定档位实测；请求/模型/编码/示波器 RAW、完整边沿、间隔/累计误差、恢复、host/Release/P3 闭合，见 T2-PROGRESS-20260922-003。仅关闭有限局部间隔实验，共同 anchor、持续同步 RUN 和 T2-CLOCK-001 仍未完成 |
| T2-CLOCK-PROBE-002 | 两标记之间铺满细分与尾格留空 | DONE | 四板指定档位的完整边沿、两端标记、尾格留空、双格间隔/低电平余量及配置恢复实测通过；host/Release/当前源码 quick P3 证据见 T2-PROGRESS-20260922-004，原严格质量告警保留；不授权 live RUN 或共同 anchor |
| T2-CLOCK-GRID-001 | 共享时间格暂存与四板无输出规划 | DONE | 四板同配置/三个指定 q 返回同一 VDC 目标及各自可复算的 TIMER1 deadline；每板七类负测、CLEAR/代际上界、会话与负载恢复、相关 host/Release/当前源码 quick P3 通过，原严格失败保留，见 T2-PROGRESS-20260922-005；不 ARM、不证明共同参考身份认证 |
| T2-CLOCK-001 | OUT1 内部细分网格与预约 deadline | IN PROGRESS | 数学/STOP 配置先按 T2-CLOCK-GRID-001 验证，活动部署与运行接入仍依赖 M0；绑定共同 anchor/周期/细分数/grid generation，以绝对格序号生成 VDC 目标并适配当前 VDC/TIMER1；整数/有理数计算不累积截断漂移，周期进位、非整除、量化、相邻格可区分、溢出/horizon/late 和网格/模型换代均验证。OUT1 不升频，诊断与正式准入分开 |
| T2-EXEC-001 | 本地硬件预约最小切片 | PENDING | 依赖 T2-CLOCK-001；沿 TriggerAO 消费意图，SYNC_IO 租约及冻结 deadline、一次输出、late 拒绝、STOP/取消事实通过；示波器验证实际输出，不能用 CPU 时间冒充 latch |
| T2-TRANSPORT-001 | 部署感知的 PREPARE/READY/fence | PENDING | M0 后；复用已测运输，确定 wire/完整性/去重/目标聚合/期限预算；旧代、错目标、缺 READY、迟到 fence 不 ARM；需新增席位时先完成 EXE-SEAT-01 审核，再冻结/登记相应跨域契约 |
| T2-MULTIBOARD-001 | 多板未来时刻输出 | PENDING | 依赖 T2-EXEC-001、T2-TRANSPORT-001、T2-FEEDBACK-001；同业务采样各角色目标时刻/偏移及各自 settle 明确，两开关切换/必要反馈/稳定后才发 VNA TRIG；反馈时刻不可预测时分阶段预约，未稳定/超时/迟到不触发。保留所有必需槽位的准备/装载/实际输出，host 不逐板发即时触发；缺节点和分区如实报告 |
| T2-FEEDBACK-001 | 每节点反馈与 VNA TRIG/REDY | PENDING | 在 T2-EXEC-001 接入时同步实现：各节点独立 feedback profile/状态，TRIG 和反馈分字段，前置许可/后置完成按协议配置；反馈窗口引用内部细分格，实际时间不吸附到格，跨格/跨窗不确定区间报告歧义。常高/常低、早到/迟到/重复/缺失、超时恢复、网格/角色换代隔离均验证；无物理反馈只保留相应证据等级 |
| T2-COMPLETE-001 | 实际 T2 latch 与完成回传 | PENDING | 依赖 T2-FEEDBACK-001；明确每角色 T2 选定边沿，保留 TRIG 与实际反馈的硬件来源、事件/模型身份、不确定度及 timeout；单线无标签反馈须有一次在飞与基线恢复策略；全部必需节点结果可追溯，缺失不填计划值 |
| T2-ACCEPT-001 | 当前切片验收与交付 | PENDING | 按固定本批范围完成相关 host/Release/TDMA 短帧/P3 与 T2 专项；UID/build/配置/原件/恢复可复算，独立审核，代码文档分离提交；只关闭实测配置，不宣布全部角色/长期/产品资格完成 |

T2-COMPLETE-001 的接口需求在本地执行设计时同步考虑，避免后补身份；实际硬件 latch 尚未就绪时，可验收输出切片，但必须保留“实际回读未完成”。

## 当前待定项与依赖

| 项目 | 当前处置 | 阻塞范围 |
|---|---|---|
| 首轮板卡绑定与反馈接线 | 角色映射已确认，UID/物理环序/IO 须读回；动态切换是必验范围，REDY/其他反馈语义不能猜测 | 真实配置装载和硬件动作；不阻塞纯模型核对 |
| 本地多槽到多板执行差异 | 参考角色 resolver 对 local_board_id 有限制，跨板需区分全局部署与本地执行授权 | T2-DEPLOY-002/003 |
| 预约运输布局与 lead time | 尚未冻结，以当前 TDMA/RefMem 容量和实测延迟选择，不能沿用分支普通 LINK 作为完整预约 | T2-TRANSPORT-001/M2 |
| 内部细分配置 | 共同原点/周期/细分数与最大 horizon 的 STOP 暂存及无输出规划正在实现；共同参考身份认证、活动部署、RUN接入仍待完成，不把实验 epoch 标签当资格 | T2-CLOCK-001，不阻塞 M0 部署 |
| 正式 VDC/校准资格 | 按 VDC-CAL-001、VDC-EVID-001、VDC-RUN-001 现状保留；不把 quick P3 视为正式资格 | 正式预约放行，不阻塞明示诊断切片 |
| 实际 T2 输入及测量能力 | 确认设备/输出边沿类型与接线、现有捕获资源、身份和有效性 | T2-COMPLETE-001；不把新通用 GPIO 观测器自动变成 M0 前置 |

## 投入与停止条件

T2-DEPLOY-001/002 已完成映射与配置事务；下一项是 T2-DEPLOY-003 的真实跨板运输和本地动作，随后才由预约执行消费已有 grid/deployment 计划。不继续扩展只读 preview、细分极限或 DPLL 工具。每片只围绕一个真实业务结果；通过后停止重复确认。异常从本地配置/路由/动作最近边界回溯，只有证据指向线序才调用 P0T。强制验收继续遵守仓库规则，允许复用的有效前置不重复制造。

GUI、自动分配、无停顿热迁移、长期压力及 RF 通路列为后续扩展；已要求的受控动态身份切换和每节点反馈属于本主线，不因简化首轮而省略。

## 统一完成定义

T2 主线只有在所选实际多板部署上完成“未来目标→真实运输→同代 READY/fence→硬件执行→各节点反馈处理/实际 T2→完成回传”，并证明身份切换、异常结果和恢复事实后才整体完成。配置可读、ACK、DMA 完成、示波器单窗以及原型仿真分别只能证明自己的范围。
