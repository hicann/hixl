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
| 10 | 文档同步更新 | 检查公开头文件注释、Python 类型/状态封装、README、示例、benchmark、脚本入参说明、构建/运行文档是否与接口、默认值、错误码、平台链路支持、环境变量和行为变更同步；新增接口必须包含功能描述、入参说明、出参说明、返回码说明，参数名说明必须与函数原型一致。 |
