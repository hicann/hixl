# LLM-DataDist开发

## 快速入门

### 整体开发流程

**简介**

为了方便后续的描述，我们对于不同的推理框架抽象为几个模块：

- 资源初始化模块。
- KV Cache管理模块。包括KV Cache的内存的创建，分配（PA场景）以及销毁。
- 模型推理模块。
- 资源释放模块。

本章节主要是介绍开发者如何在推理框架中使能LLM-DataDist的能力。

**开发流程**

1. 找到推理框架中的资源初始化模块，在该阶段中调用LLM-DataDist的初始化接口。
2. 找到推理框架中的KV Cache管理模块，调用LLM-DataDist的注册接口将自行申请的内存注册到LLM-DataDist。
3. 推理框架需要拆分出Prefill阶段和Decode阶段，对推理脚本进行分离部署，部署到不同的集群节点上。并按需对P/D进行建链。
4. 在Decode阶段执行前需要接收来自Prefill阶段的输出作为输入，同时调用LLM-DataDist提供的KV Cache传输接口拉取或推送Prefill侧缓存的KV Cache。
5. 分别执行Prefill推理脚本和Decode推理脚本。
6. 在框架资源释放模块释放LLM-DataDist相关资源。

### 环境准备

支持的形态如下：

- Atlas A2 训练系列产品/Atlas A2 推理系列产品：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server内采用HCCS传输协议时，LLM-DataDist相关接口仅支持D2D。

- Atlas A3 训练系列产品/Atlas A3 推理系列产品，该场景下采用HCCS传输协议时，LLM-DataDist相关接口不支持Host内存作为远端Cache。
- Ascend 950PR/Ascend 950DT场景下，超节点内使用UB协议，超节点间使用RoCE协议。

请参考[《CANN 软件安装》](https://hiascend.com/document/redirect/CannCommunityInstSoftware)安装驱动固件以及CANN软件。

使用hccn\_tool查询Device IP，并且进行卡间网络检测，要求各个集群上的卡间有RDMA链路连接，否则无法使能LLM-DataDist能力。hccn\_tool详细介绍请参考《[HCCN Tool 接口参考](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference)》。以下是常用命令参考。

|命令|使用场景|
|--|--|
|hccn_tool [-i %d] -link -g|获取指定Device网口Link状态。-i指定Device。样例：`hccn_tool -i 0 -link -g`|
|hccn_tool [-i %d] -ip -g|获取IP地址和子网掩码。-i指定Device。样例：`hccn_tool -i 0 -ip -g`|
|hccn_tool [-i %d] -ping -g [address %s ]|获取指定设备到目的地址的ping结果。-i指定当前server的某个Device， address指定ping的目的地址。样例：`hccn_tool -i 0 -ping -g address 192.168.2.1`|

使用LLM-DataDist过程中，还涉及到如下环境变量，具体请参见[《环境变量参考》](https://hiascend.com/document/redirect/CannCommunityEnvRef)。

|名称|使用场景|
|--|--|
|HCCL_RDMA_TC、HCCL_RDMA_SL|当客户对参数面网络做了自己的规划时，对各种业务流量规定了类型，优先级。通过这两个环境变量设置参数面集合通信流量在网络上的流量类型和优先级，以适配客户网络流量规划的要求。|
|HCCL_RDMA_RETRY_CNT、HCCL_RDMA_TIMEOUT|分别对应RDMA网卡的重试次数和重传超时时间的系数timeout。设置太大导致对网络异常反应不敏感，不能感知到网络故障。设置太小则容易造成网络闪断直接造成业务中断，不能被网卡硬件屏蔽。用户可根据自身网络情况，来设置合适的值。例如，可以根据大部分闪断的时间范围进行配置。推荐按照如下公式进行配置，以减少网络抖动带来的影响。HCCL_RDMA_TIMEOUT=log2(pull kv超时时间 * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096)，向上取整。当pull kv超时时间和HCCL_RDMA_RETRY_CNT都等于默认值时，HCCL_RDMA_TIMEOUT建议配置成15。|
|HCCL_INTRA_ROCE_ENABLE|用于配置Server内是否使用RoCE环路进行多卡间的通信。|

### 完整样例参考

本示例通过LLM-DataDist接口实现分离部署场景下KV Cache管理功能。为不同框架使用LLM-DataDist的功能提供一些参考和改造思路。

快速体验：请单击[Gitcode](https://gitcode.com/cann/hixl)，选择配套版本，从“examples/cpp”目录中获取样例。

## 功能介绍

### 链路管理

#### 建链

**功能介绍**

调用LinkLlmClusters接口在PD节点之间建立通信链路，链路主要用于KV Cache的传输。

**使用场景**

建链操作是节点之间进行数据传输的前提，建链接口采用类TCP建链流程，Server侧初始化时提供侦听信息，由Client侧发起建链，Server/Client与prompt/decoder角色无关，可以根据需求自行设置。

根据业务繁忙情况，在需要调整集群PD节点配比时，通过建链扩容节点。

**功能示例**

初始化LLM-DataDist。其中Server侧需要设置侦听的Host IP和port。

```cpp
// Server侧
LlmDataDist llm_data_dist(PROMPT_CLUSTER_ID, LlmRole::kPrompt);
std::map<AscendString, AscendString> options;
options[OPTION_DEVICE_ID] = "0";
//替换成真实IP端口
options[OPTION_LISTEN_IP_INFO] = "ip:port";
auto ret = llm_data_dist.Initialize(options);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
}
// Client侧
LlmDataDist llm_data_dist(DECODER_CLUSTER_ID, LlmRole::kDecoder);
std::map<AscendString, AscendString> options;
options[OPTION_DEVICE_ID] = "0";
auto ret = llm_data_dist.Initialize(options);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
}
```

在Client侧调用LinkLlmClusters接口发起建链操作。

```cpp
std::vector<Status> rets;
std::vector<ClusterInfo> clusters;
ClusterInfo cluster_info;
IpInfo local_ip_info{};
// 替换成本地真实IP
local_ip_info.ip = "ip";
IpInfo remote_ip_info{};
// 替换成对端真实IP
remote_ip_info.ip = "ip";
// 替换成Server侧初始化时的侦听端口
remote_ip_info.port = "port";
cluster_info.remote_cluster_id = PROMPT_CLUSTER_ID;
cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info));
cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
clusters.emplace_back(std::move(cluster_info));
auto ret = llm_data_dist.LinkLlmClusters(clusters, rets);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] LinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] LinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

**异常处理**

当调用LinkLlmClusters接口失败时，需排查两台机器网络是否连通，Host IP是否正确，port是否被占用。

#### 断链

**功能介绍**

调用UnLinkLlmClusters接口断开并清理PD之间的通信链路。

**使用场景**

当P或者D集群节点出现异常时，通过断链清理异常链路，或者需要调整集群PD节点配比时，通过断链关闭已建立的链路。

**功能示例**

调用断链方式有两种：

一种是通过在Client侧发起断链，常用在链路非故障场景。

```cpp
// clusters同建链的clusters
auto ret = llm_data_dist.UnLinkLlmClusters(clusters, rets);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnLinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] UnLinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

一种是通过在PD两侧都发起强制断链，常用在链路故障场景。

```cpp
// PD两侧
std::vector<Status> rets;
std::vector<ClusterInfo> clusters;
ClusterInfo cluster_info;
IpInfo local_ip_info;
// 替换成本地真实IP
local_ip_info.ip = "ip";
IpInfo remote_ip_info;
// 替换成对端真实IP
remote_ip_info.ip = "ip";
// 替换成Server侧初始化时的侦听端口
remote_ip_info.port = "port";
cluster_info.remote_cluster_id = PROMPT_CLUSTER_ID;
cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info)); // local_ip_infos的IP是本地的Host IP地址
cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info)); // remote_ip_infos的IP是对端的Host IP地址
clusters.emplace_back(std::move(cluster_info));
auto ret = llmDataDist.UnLinkLlmClusters(clusters, rets, 1000, true);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnLinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] UnLinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

**异常处理**

当调用UnLinkLlmClusters接口失败时，如果是网络故障场景，可改用强制断链方式。

### KV Cache管理

**功能介绍**

在调用LLM-DataDist建链前可自行申请一块内存，并调用RegisterKvCache接口注册到LLM-DataDist，在断链后调用UnregisterKvCache接口进行解注册。

KV Cache管理涉及的主要接口及功能如下：

|接口名称|功能|
|--|--|
|RegisterKvCache|注册本地Cache。|
|UnregisterKvCache|解注册本地Cache。|
|PullKvCache|从远端节点拉取Cache到本地Cache，与角色无关。|
|PullKvBlocks|在传输Blocks Cache场景下，通过配置block列表的方式，从远端节点拉取Cache到本地Cache，与角色无关。|
|PushKvCache|从本地节点推送Cache到远端节点，与角色无关。|
|PushKvBlocks|在传输Blocks Cache场景下，通过配置block列表的方式，从本地节点推送Cache到远端节点，与角色无关。|

**使用场景**

主要用于分布式集群间的KV Cache传输和搬移。

**功能示例（一般Cache传输场景）**

本示例介绍一般Cache传输场景下接口的使用，主要涉及KV Cache的注册、解注册、传输。如下将根据业务角色给出伪代码示例。

1. P侧和D侧根据[建链](llm-datadist_develop.md#建链)章节的示例完成LLM-DataDist的初始化并申请内存进行注册。

    ```cpp
    void OnError(LlmDataDist &llm_data_dist, int64_t cache_id)
    {
        if (cache_id > 0) {
            (void) llmDataDist.UnregisterKvCache(cache_id);
        }
        llm_data_dist.Finalize();
    }
    CacheDesc kv_cache_desc{};
    kv_cache_desc.num_tensors = NUM_TENSORS;
    kv_cache_desc.data_type = DT_INT32;
    kv_cache_desc.shape = {8, 16};
    std::vector<uint64_t> addrs;  // 按需填充
    int64_t cache_id = -1;
    auto ret = llm_data_dist.RegisterKvCache(kv_cache_desc, addrs, {}, cache_id);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. P侧和D侧根据[建链](llm-datadist_develop.md#建链)章节的示例完成LLM-DataDist的建链操作。

3. P/D侧按需对KV Cache内存进行传输，若失败，则需要释放对应的资源，Cache传输有两种方式。

    - 调用PullKvCache接口拉取KV Cache。

    ```cpp
    // P侧进行全量推理写入cache，通知D侧可以pull
    // D侧拉取cache
    CacheIndex cache_index{PROMPT_CLUSTER_ID, 1, 0};
    ret = llm_data_dist.PullKvCache(cache_index, cache, 0);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PullKvCache failed, ret = %u\n", ret);
        return -1;
    }
    // 进行增量推理
    ```

    - 调用PushKvCache接口推送KV Cache。

    ```cpp
    // P侧一层计算完可在传输线程立即推送，以实现将大部分传输时间和计算重叠。
    CacheIndex dst_cache_index{DECODE_CLUSTER_ID, 1, 0};
    KvCacheExtParam ext_param{};
    ext_param.src_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    ext_param.dst_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    // 每层tensor数量，可根据实际模型修改
    ext_param.tensor_num_per_layer = 2;
    ret = llm_data_dist.PushKvCache(cache, dst_cache_index, 0, -1, ext_param);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PushKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

4. 业务退出时，P侧和D侧根据[断链](llm-datadist_develop.md#断链)章节的示例进行断链，调用UnregisterKvCache进行解注册并调用finalize接口释放资源。

**功能示例（Blocks Cache传输场景）**

本示例介绍Blocks Cache（将Cache使用块状形式管理）传输场景下接口的使用，主要涉及KV Cache的注册、传输。如下将根据业务角色给出伪代码示例。

1. P侧和D侧根据集群[建链](llm-datadist_develop.md#建链)的示例完成LLM-DataDist的初始化和建链操作。
2. 在P侧和D侧模型的每层按照计算好的num\_block数量申请KV Cache，并注册给LLM-DataDist。上层框架根据不同请求对创建的num\_block大小的KV Cache进行复用，业务结束后释放申请的内存。

    ```cpp
    void OnError(LlmDataDist &llm_data_dist, int64_t cache_id)
    {
        if (cache_id > 0) {
            (void) llmDataDist.UnregisterKvCache(cache_id);
        }
        llm_data_dist.Finalize();
    }
    CacheDesc kv_cache_desc{};
    kv_cache_desc.num_tensors = NUM_TENSORS;
    kv_cache_desc.data_type = DT_INT32;
    kv_cache_desc.shape = {8, 16};
    std::vector<uint64_t> addrs;  // 按需填充
    int64_t cache_id = -1;
    auto ret = llm_data_dist.RegisterKvCache(kv_cache_desc, addrs, {}, cache_id);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

3. P侧有新请求进来后，为每个请求分配好对应的block\_index，该功能由推理框架提供。模型推理完之后，该请求对应的KV Cache就在对应的block\_index所在的内存上，将模型输出和请求对应的block\_table传输给D侧推理模型作为输入。
4. D侧有新请求进来后，给每个请求分配好对应的block\_index_，_然后调用PullKvBlocks接口，根据P侧的block\_index和D侧的block\_index的对应关系，将KV Cache传输到指定位置。

    - 在D侧调用PullKvBlocks接口拉取KV Cache。

    ```cpp
    // P侧进行全量推理写入cache，通知D侧可以拉取cache
    // D侧拉取cache
    CacheIndex cache_index{PROMPT_CLUSTER_ID, 1, 0};
    std::vector<uint64_t> prompt_blocks = {0, 1, 2, 3};
    std::vector<uint64_t> decoder_blocks = {3, 2, 1, 0};
    auto ret = llm_data_dist.PullKvBlocks(cache_index, cache, prompt_blocks, decoder_blocks);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PullKvBlocks failed, ret = %u\n", ret);
        return -1;
    }
    // 进行增量推理
    ```

    - 在P侧调用PushKvBlocks接口推送KV Cache。

    ```cpp
    // P侧一层计算完可在传输线程立即推送，以实现将大部分传输时间和计算重叠。
    CacheIndex dst_cache_index{DECODE_CLUSTER_ID, 1};
    KvCacheExtParam ext_param{};
    ext_param.src_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    ext_param.dst_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    // 每层tensor数量，可根据实际模型修改
    ext_param.tensor_num_per_layer = 2;
    std::vector<uint64_t> prompt_blocks = {0, 1, 2, 3};
    std::vector<uint64_t> decoder_blocks = {3, 2, 1, 0};
    ret = llm_data_dist.PushKvBlocks(cache, dst_cache_index, prompt_blocks, decoder_blocks, ext_param);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PushKvBlocks failed, ret = %u\n", ret);
        return -1;
    }
    ```

5. 业务退出时，P侧和D侧根据[断链](llm-datadist_develop.md#断链)章节的示例进行断链，调用UnregisterKvCache进行解注册并资源释放。

**异常处理**

- 错误码LLM\_KV\_CACHE\_NOT\_EXIST表示对端KV Cache不存在，需要检查对端进程是否异常或者对应KV Cache的请求是否推理完成。该错误不影响其他请求流程，确认流程后可以重试。
- 错误码LLM\_WAIT\_PROCESS\_TIMEOUT或LLM\_TIMEOUT表示pull KV超时，说明链路出现问题，需要重新断链，再尝试建链。
- 错误码LLM\_NOT\_YET\_LINK表示与远端cluster没有建链。

## 专题

### 角色切换

**使用场景**

主要用于在PD集群节点数量固定的场景下，由于业务的变化，期望PD集群节点间可以相互切换，充分利用资源。

**涉及的接口**

调用SetRole接口对LLM-DataDist的角色进行切换，并通过option切换Client或者Server。

**功能示例**

示例由角色Decoder切换为Prompt，从Client切换为Server。切换角色后，根据业务功能调用LLM-DataDist其他接口。

1. LLM-DataDist初始化。

    ```cpp
    LlmDataDist llm_data_dist(DECODER_CLUSTER_ID, LlmRole::kDecoder);
    std::map<AscendString, AscendString> options;
    options[OPTION_DEVICE_ID] = "0";
    auto ret = llm_data_dist.Initialize(options);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. 调用SetRole接口进行角色切换，Client切换为Server需指定侦听的Host IP和端口；Server切换为Client时，option不指定Host IP和端口。

    ```cpp
    std::map<AscendString, AscendString> options;
    //替换成真实IP端口
    options[OPTION_LISTEN_IP_INFO] = "ip:port";
    llmDataDist.SetRole(LlmRole::kPrompt, options);
    ```

**异常处理**

异常处理请参考LLM-DataDist错误码。
