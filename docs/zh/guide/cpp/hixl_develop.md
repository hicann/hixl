# HIXL开发

## HIXL简介

HIXL（Huawei Xfer Library）即昇腾单边通信库，面向集群场景提供高性能、零拷贝的点对点数据传输的能力，并通过简易API开放给用户。

在分布式内存池的场景下，上层框架，比如Mooncake Store具备KV Cache管理的能力，但是在传输层需要依赖本地地址和远端地址进行内存的远端读写。HIXL即昇腾单边通信库在该场景下应运而生，提供了一个纯粹的基于本地地址和远端地址的传输能力。

**表1**  HIXL和LLM-Datadist比较

|维度|HIXL|LLM-Datadist|
|--|--|--|
|使用场景|主要用于分布式集群间的内存传输。|主要用于分布式集群间的KV Cache传输和搬移。|
|主要功能|链路管理，包括断链和建链。内存传输，包括内存的注册、解注册、传输。中转传输|链路管理，包括断链和建链。KV Cache传输，包括KV Cache的注册、解注册、传输。|

## 快速入门

### 整体开发流程

**简介**

为了方便后续的描述，我们对于不同的推理框架抽象为几个模块：

- 资源初始化模块。
- 内存管理模块。包括内存的创建，分配以及销毁。
- 模型推理模块。
- 资源释放模块。

本章节主要是介绍开发者如何在推理框架中使能HIXL的能力。

**开发流程**

1. 找到推理框架中的资源初始化模块，在该阶段中调用HIXL的初始化接口。
2. 找到推理框架中的内存管理模块，调用HIXL的注册接口将自行申请的内存注册到HIXL。
3. 推理框架要能够拆分出Prefill阶段和Decode阶段，对推理脚本进行分离部署，部署到不同的集群节点上。在Decode阶段执行前需要接收来自Prefill阶段的输出作为输入，同时调用HIXL内存传输接口拉取对端内存或推送本地内存到对端。
4. 分别执行Prefill推理脚本和Decode推理脚本。
5. 在框架资源释放模块释放HIXL相关资源。

### 环境准备

支持的产品形态如下：

<!-- npu="910b" id1 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品。
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR/Ascend 950DT场景下，超节点内使用UB协议，超节点间使用RoCE协议。
<!-- end id3 -->

请参考[《CANN 软件安装》](https://hiascend.com/document/redirect/CannCommunityInstSoftware)安装好驱动固件以及CANN软件。

使用hccn\_tool查询Device IP，并且进行卡间网络检测，要求各个集群上的卡间有RDMA链路连接，否则无法使能HIXL能力。hccn\_tool详细介绍请参考《[HCCN Tool 接口参考](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference)》。

使用LLM-DataDist过程中，还涉及到如下环境变量，具体请参见[《环境变量参考》](https://hiascend.com/document/redirect/CannCommunityEnvRef)。

|名称|使用场景|
|--|--|
|HCCL_RDMA_TC、HCCL_RDMA_SL|当客户对参数面网络做了自己的规划时，对各种业务流量规定了类型，优先级。通过这两个环境变量设置参数面集合通信流量在网络上的流量类型和优先级，以适配客户网络流量规划的要求。|
|HCCL_RDMA_RETRY_CNT、HCCL_RDMA_TIMEOUT|分别对应RDMA网卡的重试次数和重传超时时间的系数timeout。设置太大导致对网络异常反应不敏感，不能感知到网络故障。设置太小则容易造成网络闪断直接造成业务中断，不能被网卡硬件屏蔽。用户可根据自身网络情况，来设置合适的值。例如，可以根据大部分闪断的时间范围进行配置。推荐按照如下公式进行配置，以减少网络抖动带来的影响。HCCL_RDMA_TIMEOUT=log2(pull kv超时时间 * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096)，向上取整。当pull kv超时时间和HCCL_RDMA_RETRY_CNT都等于默认值时，HCCL_RDMA_TIMEOUT建议配置成15。|
|HCCL_INTRA_ROCE_ENABLE|用于配置Server内是否使用RoCE环路进行多卡间的通信。|
|HCCL_INTRA_PCIE_ENABLE|用于配置Server内是否使用PCIe环路进行多卡间的通信。|
|HCCL_INTER_HCCS_DISABLE|用于配置超节点模式组网中超节点内的通信链路类型。|

### 完整样例参考

本示例通过HIXL接口实现分离部署场景下内存传输功能。为不同框架使用HIXL的功能提供一些参考和改造思路。

快速体验：请单击[Gitcode](https://gitcode.com/cann/hixl)，选择配套版本，从“examples/cpp”目录中获取样例。

## 功能介绍

### 链路管理

#### 建链

**功能介绍**

调用Connect接口向Server侧建立通信链路，链路主要用于数据的传输，其中P/D均可作为Server或者Client，按需设置。

**使用场景**

建链操作是节点之间进行数据传输的前提，建链接口采用类TCP建链流程，Server初始化时提供侦听信息，由Client侧发起建链。

**功能示例**

1. 初始化HIXL。其中Server侧需要设置侦听的Host IP和port。

    ```cpp
    // Server侧
    Hixl engine;
    std::map<AscendString, AscendString> options;
    AscendString local_engine = "ip:port"; // 替换成真实IP端口；是HIXL的唯一标识，端口为有效值
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    // Client侧
    Hixl engine;
    std::map<AscendString, AscendString> options;
    AscendString local_engine = "ip:port"; // 替换成真实IP；是Hixl的唯一标识，端口可不设置或设置为负数，此时表示不侦听
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. 在Client侧调用Connect发起建链操作。

    ```cpp
    AscendString remote_engine = "ip:port"; // 替换成需要与之建链的Server的local engine
    auto ret = engine.Connect(remote_engine);
    if (ret != SUCCESS) {
        printf("[ERROR] Connect failed, ret = %u\n", ret);
        return -1;
    }
    ```

**异常处理**

当调用建链失败时，需排查两台机器网络是否连通，Host IP是否正确，port是否被占用。

更多异常处理请参考HIXL错误码。

#### 断链

**功能介绍**

调用Disconnect接口断开并清理通信链路。

**使用场景**

当P或者D集群节点出现异常时，通过断链清理异常链路，或者需要调整集群PD节点配比时，通过断链关闭已建立的链路。

**功能示例**

通过在Client侧发起断链。

```cpp
AscendString remote_engine = "ip:port"; // 替换成需要与之断链的Server的local engine
auto ret = engine.Disconnect(remote_engine);
if (ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    return -1;
}
```

**异常处理**

更多异常处理请参考HIXL错误码。

### 内存管理

**功能介绍**

内存管理涉及的主要接口及功能如下：

|接口名称|功能|
|--|--|
|RegisterMem|注册内存。需要在建链前由Client和Server分别调用。|
|DeregisterMem|解注册内存。需要在断链后由Client和Server分别调用。|
|TransferSync|从远端地址读取内存到本地，或者将本地内存推送到远端地址对应的内存上。|

**使用场景**

主要用于分布式集群间的内存传输。

**功能示例**

本示例介绍传输接口的使用，主要涉及内存的注册、注销、传输。如下将根据业务角色给出伪代码示例。

1. Client侧和Server侧根据[建链](hixl_develop.md#建链)章节的示例完成HIXL的初始化。
2. Client侧和Server侧为每个请求申请对应大小的内存并注册，若失败，则需要释放对应的资源。该操作需要在建链前进行。

    ```cpp
    void OnError(Hixl &engine, const std::vector<MemHandle> &handles)
    {
        for (auto handle : handles) {
            (void) engine.DeregisterMem(handle);
        }
        engine.Finalize();
    }
    
    MemDesc desc{};
    desc.addr = reinterpret_cast<uintptr_t>(addr);
    desc.len = len;
    MemHandle handle = nullptr;
    auto ret = engine.RegisterMem(desc, MEM_HOST, handle);
    if (ret != SUCCESS) {
        printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
        return -1;
    }
    ```

3. 内存传输，包含两种方式

    在Atlas A3 训练系列产品/Atlas A3 推理系列产品需要使用HCCS进行D2RH、RH2D传输的场景。可以通过开启OPTION\_ENABLE\_USE\_FABRIC\_MEM配置选项来获得最佳的传输性能。

    - 在Client侧调用TransferSync接口，设置operation参数为READ，即从远端地址读取内存到本地，其中，本地地址和远端地址需在建链前分别在本地和远端完成注册。

        ```cpp
        TransferOpDesc desc{reinterpret_cast<uintptr_t>(&local), reinterpret_cast<uintptr_t>(remote), size};
        auto ret = engine.TransferSync(remote_engine, READ, {desc});
        if (ret != SUCCESS) {
            printf("[ERROR] TransferSync read failed, ret = %u\n", ret);
            return -1;
        }
        ```

    - 在Client侧调用TransferSync接口，设置operation参数为WRITE，即将本地地址对应内存推送到远端，其中，本地地址和远端地址需在建链前分别在本地和远端完成注册。

        ```cpp
        TransferOpDesc desc{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), size};
        auto ret = engine.TransferSync(remote_engine, WRITE, {desc});
        if (ret != SUCCESS) {
            printf("[ERROR] TransferSync write failed, ret = %u\n", ret);
            return -1;
        }
        ```

4. 业务退出时，P侧和D侧根据[断链](hixl_develop.md#断链)章节的示例进行断链和调用finalize接口释放资源。

**异常处理**

- 错误码TIMEOUT表示传输超时，说明链路出现问题，需要重新断链，再尝试建链。
- 错误码NOT\_CONNECTED表示与远端没有建链。

更多异常处理请参考HIXL错误码。

### 中转传输

**使用场景**

中转传输是指使用中转buffer进行传输。当使用RDMA进行Host To Host等直传传输注册内存大小有限制时，需要使用该功能。

**功能示例**

本示例使用中转传输功能进行Host To Host传输。

1. Client侧和Server侧根据[建链](hixl_develop.md#建链)章节的示例完成HIXL的初始化和建链。区别在于初始化阶段需要额外配置一个中转内存池的配置选项"BufferPool"。

    ```cpp
    Hixl engine;
    std::map<AscendString, AscendString> options;
    options["BufferPool"] = "4:8";
    AscendString local_engine = "ip:port"; // 替换成真实IP端口；是Hixl的唯一标识，端口为有效值
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. 和[内存管理](hixl_develop.md#内存管理)章节一样进行内存传输，区别在于不需要调用RegisterMem接口注册Host内存，注册了则会走直传模式，不注册会采用中转传输模式。

**异常处理**

- 错误码TIMEOUT表示传输超时，说明链路出现问题，需要重新断链，再尝试建链。
- 错误码NOT\_CONNECTED表示与远端没有建链。
