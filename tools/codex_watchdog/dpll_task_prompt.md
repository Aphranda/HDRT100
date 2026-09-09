**目标：在 HAOFV 架构下建立可配置的多节点 DPLL 控制框架，按照VDC TODO P0A执行 。**

物理拓扑保持四节点环形，TDMA 与校准流程继续负责时延测量，不改变既有训练结果。每块板保留完整 PI 能力，但运行时可配置为 `MASTER` 或 `FOLLOWER`，初始验收配置为 `1 主 3 从`，后续支持 `2 主 2 从`、`3 主 1 从` 及主机动态切换。

控制规则：

- `MASTER`：由本地 TDMA evidence 驱动鉴相器、PI、DCO，并发布频率字、相位快照、锁定状态和共同生效时间。
- `FOLLOWER`：旁路本地鉴相器、PI、积分器和本地锁定升级，只在 Core1 确定性边界复制指定主机的已验证命令。
- 从机命令缺失、陈旧、来源错误、CRC/时序错误时，保持上一稳定输出，禁止回退到本地 PI。
- 主站选择、跟随源、控制代次和应用状态必须可读、可切换、可回退。

时延规则：

- 校准和训练只测量有向链路时延，不改变测量流程。
- DPLL 使用 TDMA 数据反向路径的时延；例如 `NO2` 的反向路径使用 `NO2 -> NO1`。
- 通信延迟通过共同绝对生效时间消除，不能使用接收时刻直接更新 DDS。
- 在 TDMA/RefMem 尚未定义共同生效时间、源节点和序列语义前，不得接入从机实时应用。

本地晶振驯服：

- 作为独立慢速通道，仅调整底层 oscillator trim。
- 不得写入 DDS 相位累加器，不得替换主机 DCO 命令，不得重新启用从机 PI。
- 必须具备限幅、限速、最小更新间隔、stale/fault freeze、requested/applied generation 和硬件能力报告。
- 驯服失败只能冻结最近可信值，不得提升 DPLL lock 或 quality。

HAOFV owner 边界：

- Core0/SCPI：配置、暂存、读回、显式 Flash 存储。
- Core1/SyncDpllFB：在 service boundary 应用角色和主机命令。
- TDMA：拥有 process image、时序、序列和 CRC。
- RefMem：保存稳定的按源节点命令副本，不拥有控制决策。
- Calibration：只负责有向时延和 bias 测量。
- Hardware driver：负责晶振执行器。
- VDC Domain：负责角色状态、PI/DCO 状态和质量规则。

执行顺序：

1. 完成并验证 `MASTER/FOLLOWER` Domain 边界。
2. 冻结 TDMA/RefMem 的源节点、序列和共同生效时间契约。
3. 实现按 source slot 的命令保留和 Core1 定时应用。
4. 接入 Flash 默认值、SCPI 配置和显式存储。
5. 接入独立晶振驯服通道。
6. 进行 `1M3F`、`2M2F`、`3M1F`、主机切换及故障注入验证。
7. 通过当前源码指纹下的软件测试、构建、P3/HIL 和长期观测后，才评估正式锁定。

验收原则：

> 主站“算”，从站“抄”；延迟“测”，相位“盖”；晶振“慢调”，不夺取相位控制权。

`50 ns` 只能作为实测验收门限，不能作为架构修改后的预设保证。
