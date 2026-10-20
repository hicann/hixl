# HIXL 代码检视重点

> **适用场景**：HIXL 代码重点检视项适用于 HIXL 仓所有代码。

## 领域背景

HIXL 是昇腾单边通信库，面向集群场景提供简单、可靠、高效的点对点数据传输能力，同时包含 HIXL Engine、LLM-DataDist、HIXL_CS 的 C++/C/Python 接口。

该仓库覆盖 RDMA/HCCS/RoCE/UB/FabricMem 等链路与 A2/A3/950 等多种产品形态。因此检视时不仅要关注资源泄漏、内存问题、并发问题和时序问题，还要重点排查 ABI 稳定性、建链状态闭合、平台支持矩阵以及文档正确性。


## 重点检查清单

按下表顺序逐项检查，对每项给出 `✅/⚠️/❌` 和具体发现。

| 编号 | 检查维度           | 重点检查内容 |
|----|----------------|--------|
| 1  | 设备内存对齐申请释放 | 检查 `aclrtMalloc/aclrtFree`、`aclrtMallocHost/aclrtFreeHost`、`aclrtMallocPhysical/aclrtFreePhysical`、FabricMem 映射/解映射、`AlignedPtr`、`ScalableAllocator` 等申请释放是否成对；设备/Host 类型、长度、对齐、溢出和失败回滚是否完整；异常路径是否通过 guard 或 RAII 释放资源。 |
| 2  | 多线程并发竞态 | 检查 `HixlEngine`、`HixlServer`、`HixlCSClient`、`ChannelManager`、`TransferPool`、Cache 管理和统计模块中的 map/vector/handle/状态字段是否受锁或原子保护；锁顺序是否一致；条件变量是否有谓词；Finalize/Destroy 与异步线程、回调、传输请求并发时是否存在 use-after-free、重复释放或死锁。 |
| 3  | 通信资源申请释放 | 检查 HCCL/HCOMM channel、thread、stream、socket、epoll、listen fd、endpoint、server/client handle、mem handle、complete handle 等资源是否按 Create/Destroy、Connect/Disconnect、Register/Deregister、Alloc/Free 成对闭合；建链失败、半连接、超时、中断和重复调用路径是否清理本地与远端资源。 |
| 4  | 错误传播与状态码一致性 | 检查 `aclError`、HCCL/HCOMM 返回值、`hixl::Status`、`llm_datadist::Status`、`HixlStatus`、Python 异常/状态对象是否在调用栈中保真传播；宏封装是否覆盖失败日志和返回码；超时、参数错误、不支持、资源耗尽、未建链/已建链等场景是否返回稳定且可诊断的错误码，而不是静默成功或统一吞成 `FAILED`。 |
| 5  | 公共 API/ABI 与 ACL 接口规范 | 检查 `include/hixl`、`include/adxl`、`include/llm_datadist`、`include/cs`、Python wrapper 的公开函数签名、默认参数、错误码数值、枚举取值、结构体字段顺序、`reserved` 扩展位和可见性是否破坏兼容；ACL 风格接口命名是否使用规范前缀、对象、操作动词、宾语，创建/销毁、申请/释放、打开/关闭等对称操作是否成对。 |
| 6  | 生命周期与状态机闭合 | 检查 `Initialize/Finalize`、`Create/Destroy`、`Connect/Disconnect`、`Link/Unlink`、`Register/Deregister`、`SetRole`、FSM idle/send/receive 状态转换是否闭合；未初始化、重复初始化、重复释放、角色切换、断链重连、析构兜底和错误回滚是否保持对象状态一致。 |
| 7  | 注册内存语义 | 检查 `MemDesc`、`CommMem`、`Cache`、`CacheDesc`、`RegisterCfg`、KV Cache 注册信息的地址、长度、shape、dtype、placement、Host/Device 类型和跨层 tensor 数是否校验完整；重复注册、地址重叠、传输中解注册、cache id 复用、远端导入/导出描述释放、`QueryRegisterMemStatus` 状态流转是否有明确保护。 |
| 8  | 异步传输与完成态语义 | 检查 `TransferAsync/GetTransferStatus`、`TransferSync`、`BatchPutAsync/BatchGetAsync`、`QueryCompleteStatus`、FabricMem/BufferTransfer 请求跟踪和 complete handle 生命周期；完成态是否单调且一致；WAITING/COMPLETED/TIMEOUT/FAILED 是否可重复查询；超时、断链、Finalize、回调失败和 in-flight 请求是否安全收敛。 |
| 9  | 集群拓扑与平台链路兼容性 | 检查 `ClusterInfo`、`IpInfo`、listen 信息、`local_ip_infos/remote_ip_infos`、rank table v1/v2、cluster id、rank、endpoint、local comm res、IPv4/IPv6、RoCE/UB comm id 解析是否合法；空配置、重复 cluster、乱序 rank、缺失本地通信资源是否严格报错；A2/A3/950PR/950DT、RDMA/HCCS/RoCE/UB/FabricMem/HCCL/HCOMM 支持矩阵是否被意外放开或收窄，不支持场景是否显式返回错误。 |
| 10 | 文档同步更新 | 检查公开头文件注释、Python 类型/状态封装、README、示例、benchmark、脚本入参说明、构建/运行文档是否与接口、默认值、错误码、平台链路支持、环境变量和行为变更同步；新增接口必须包含功能描述、入参说明、出参说明、返回码说明，参数名说明必须与函数原型一致。**中英文同步核对范围为除 `docs/{zh,en}/api/**` 与 `docs/{zh,en}/guide/**` 之外的文档**：`api` 由对外资料/翻译流程维护，`guide` 不翻译，均不检视其同步性；其余（`design/`、`contributions/`、`README`、`quick_start`、`build`、`FabricMem` 等）仍要求中英文同步。表述与定位细节由第 15 项承接。 |
| 11 | 对外接口文档一致性 | 检查 `include/` 下公开头文件变更时，`docs/zh/api/cpp/` 中是否有对应的接口说明文档且内容正确。具体包括：头文件变更时对应 `docs/zh/api/cpp/` 文档是否同步更新；文档中的函数原型与头文件签名是否严格一致（返回类型、参数类型、参数名、默认值、const 引用）；每个公开 API 是否有完整的 `@brief`/`@param`/`@return` Doxygen 注释且参数名与原型一致；接口说明文档章节是否齐全（产品支持情况、功能说明、函数原型、参数说明、返回值说明、约束说明），依据 api-doc-generator 规则 #1-#5。注释质量细节由第 12 项承接。 |
| 12 | 对外头文件注释质量 | 检查 `include/` 下 `.h` 文件中新增/修改的公共 API 函数 Doxygen 注释是否符合 [header-comment-spec.md](./header-comment-spec.md) 中的 HC-1 至 HC-9；`@brief`、`@param [in]/[out]`、`@return` 是否完整、一致；数值参数是否标注单位；结构体字段是否有注释。仅检查 diff 新增/修改行，不追溯存量问题。 |
| 13 | 设计与经济性 | 对 diff 中每个**新增**的形参、结构体、公开接口、函数分支、校验点逐一反问是否必须存在：能否合并或删除；是否与其他层重复；其携带的数据是否可由已有入参派生；常量条件下的分支是否恒真/恒假（死代码）；是否需要调用方额外合并状态才能决定返回码（应改为各生成函数自闭环、逐级 fail-fast）。 |
| 14 | 新增配置项与参数 | 检查新增 option/CLI 参数：是否曾发布（用基线如 `git grep upgrade/master` 确认，未发布则不做反向兼容校验与废弃提示）；参数名与其承载的取值层是否一致（如 `--comm_resource_config` 应接收该配置块的内层键并由工具补前缀）；实现、help、readme、示例四者是否一致。 |
| 15 | 文档表述与定位 | 检查对外文档是否站在用户使用 API 的视角、按**对外语义**表述：**禁止内部实现流程与内部符号**（内部函数名、内部判定顺序、"整段失败返回参数错误"等），内部触发条件须翻译为对外生效条件（如内部"已有非空 endpoint_list 时跳过"→对外"仅在未指定 LocalCommRes 时生效"）；option 描述只描述该 option 自身功能，不与某个内部自动生成流程绑定；机型信息用**正式全名不简写**（禁 `A5`，用 `Ascend 950PR&950DT` 等）并打**产品型号标签**——沿用接口文档已有的产品型号注释标签写法：属性值取产品型号，标签 ID 用 `id` 加数字且开始/结束标签成对，且**不得落在表格的某一行内**，须放在表格下方「产品支持情况」说明条目上对该条说明单独打标签；字段级说明（含仅预留项如 `avail`）与使用示例齐全。 |
