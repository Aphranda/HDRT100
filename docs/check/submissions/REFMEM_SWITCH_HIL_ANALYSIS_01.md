# 分析：M6 开关 owner 四板 HIL 证据

Status: Active
Domain: REFMEM
Canonical: `docs/check/submissions/REFMEM_SWITCH_HIL_ANALYSIS_01.md`
Related: `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/check/submissions/REFMEM_READ_CROSS_REVIEW_01.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-11-09

## 1. 目的

为 M6“开关 fact 只属 SWITCH_A/B”提供可追溯的四板证据，并回答交叉审核 `d5131cfa`
指出的“归档缺 stage/activate 与部署读回”的问题。原始串口记录在
`out/refmem_read/switch_role_hil_20261109.txt` 与
`out/refmem_read/switch_role_hil_20261109_full.txt`（`out/` 被 git 忽略，本文件记录其内容摘要与生成方式）。

## 2. 测试部署

主机用四台架序列号作 `board_uid`、角色顺序 COUNTER/SWITCH_A/GATEWAY/SWITCH_B，
CRC 用 `zlib.crc32` 对 LE u32 序列（generation + 每行 uid_lo/uid_hi/slot/in/out/fb）计算：

| 角色 | board_uid (hex) | slot | input | output | feedback |
|---|---|---|---|---|---|
| COUNTER | `0x0010071E65B5CB38` | 0 | 1 | 0 | 0 |
| SWITCH_A | `0xFB276192BEF9CCE1` | 1 | 0 | 7 | 0 |
| GATEWAY | `0x2BD5090FE009FA2A` | 2 | 1 | 8 | 1 |
| SWITCH_B | `0xA1E549202D18ED6A` | 3 | 0 | 1 | 0 |

`generation=1800000001`，`crc=1450950601`。四板逐板执行
`TRIGger:REServation:DEPloyment:STAGe 1800000001,1450950601,...` → `1,0`，
再 `ACTivate 1800000001,1450950601` → `1,0`；随后 `DEPloyment?` 在每板返回
`active=1 / generation=1800000001 / crc=1450950601`，且本板 `board_uid`/role/slot 与上表一致
（COM4=SWITCH_A slot1、COM3=GATEWAY slot2、COM6=SWITCH_B slot3、COM5=COUNTER slot0）。
测试后四板已 `DEPloyment:CLEar`，`DEPloyment?` 回到 `active=0`。

## 3. 结果（build `20261109072222`）

| 板 | 角色 | 命令 | `CTRL:STATus?` | `READ:SWITch1?` |
|---|---|---|---|---|
| COM4 | SWITCH_A | `45,2,2,1,4` | state 4 ACKED，ack=2，reason 0 | 5→4 |
| COM6 | SWITCH_B | `45,8,8,1,4` | state 4 ACKED，ack=8，reason 0 | 3→4 |
| COM5 | COUNTER | `45,1,1,1,4` | state 5 NACKED，nack=1，reason 12 | 1→1 |
| COM3 | GATEWAY | `45,4,4,1,4` | state 5 NACKED，nack=4，reason 12 | 1→1 |
| COM5 | 广播 | `45,15,15,1,6` | `-200,"NOT_ONLINE_SAFE"`（环未运行） | — |

字段序按 `SYST:CTRL:STATus?` 的 13 项（seq,crc,state,command_seq,target,required,ack,nack,
busy,timeout,last_reason,last_reason_slot,last_completed_seq）逐项核对。

## 4. 限定与残余

- **build 绑定**：证据 build `20261109072222` 来自一次失败的 scope P3 运行；其固件源指纹与
  提交 receipt `20261109063728` 的源树一致（差异只是未跟踪的 `p3_bench_scope.json` 验收输入），
  因此行为可迁移，但该 build 本身没有 receipt 绑定。
- **广播聚合**：因环未运行，`0xF` 只得到 `NOT_ONLINE_SAFE`，聚合 ACK/NACK 未证，留 M7。
- **output_mask 仅形状校验**：`trigger_switch_handler` 不读部署行的 `output_mask`，
  而 `sync_io_sequence_set_switch(1,..)` 固定驱动 3 条 SIO（mask 0x7）；SWITCH_B 行声明
  `output=1` 与实际驱动不一致，属部署/资源命名空间的待澄清项（不阻塞 owner 判定）。
- **RAM-only**：部署不持久化，复位即失；测试后已清除，避免影响后续运行。

## 5. 结论

“SWITCH_A/B 接受并物理回读、COUNTER/GATEWAY 拒绝 reason 12”成立，且部署
gen/CRC/role 已由 `DEPloyment?` 直接证明；广播聚合与 build 凭证绑定仍待补。
