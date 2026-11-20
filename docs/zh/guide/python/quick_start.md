# 快速入门

## 整体开发流程

**简介**

为了方便后续的描述，我们对于不同的推理框架抽象为几个模块：

- 资源初始化模块。
- KV Cache管理模块。包括KV Cache内存的创建，分配（PA场景）以及销毁。
- 模型推理模块。
- 资源释放模块。

本章节主要是介绍开发者如何在推理框架中使能LLM-DataDist的能力。

**开发流程**

1. 找到推理框架中的资源初始化模块，在该阶段中调用LLM-DataDist的初始化接口和建链接口。
2. 找到推理框架中的KV Cache管理模块，在该阶段中调用LLM-DataDist的KV Cache注册接口，LLM-DataDist的注册接口将自行申请的内存注册到LLM-DataDist。
3. 推理框架需要拆分出Prefill阶段和Decode阶段，对推理脚本进行分离部署，部署到不同的集群节点上。在Decode阶段执行前需要接收来自Prefill阶段的输出作为输入，同时调用LLM-DataDist提供的KV Cache传输接口拉取或推送Prefill侧缓存的KV Cache。
4. 分别执行Prefill推理脚本和Decode推理脚本。
5. 在框架资源释放模块释放LLM-DataDist相关资源。

## 环境准备

支持的形态如下：

<!-- npu="910b" id1 -->
- Atlas A2系列产品：针对Atlas A2系列产品，仅支持Atlas 800I A2推理服务器、A200I A2 Box异构组件。该场景下Server内采用HCCS传输协议时，仅支持D2D。
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品，该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR&950DT系列产品场景下，超节点内使用UB协议，超节点间使用RoCE协议。
<!-- end id3 -->

请参考《[CANN软件安装](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/latest/softwareinst/instg/instg_0000.html?OS=openEuler&InstallType=netyum)》安装好驱动固件以及CANN软件。

异构场景下，接收方和发送方的MTU(Maximum Transmission Unit，最大传输单元)配置需要保持一致，MTU的查询和配置请参考《[HCCN Tool 接口参考](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference)》。

使用hccn\_tool查询Device IP，并且进行卡间网络检测，要求各个集群上的卡间有RDMA链路连接，否则无法使能LLM-DataDist能力。hccn\_tool详细介绍请参考《[HCCN Tool 接口参考](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference)》。以下是常用命令参考。

|命令|使用场景|
|--|--|
|hccn_tool [-i %d] -link -g|获取指定Device网口Link状态。-i指定Device。样例：`hccn_tool -i 0 -link -g`|
|hccn_tool [-i %d] -ip -g|获取IP地址和子网掩码。-i指定Device。样例：`hccn_tool -i 0 -ip -g`|
|hccn_tool [-i %d] -ip -inet6 -g|获取IPv6地址和子网掩码。-i指定Device。样例：`hccn_tool -i 0 -ip -inet6 -g`|
|hccn_tool [-i %d] -ping -g [address %s ]|获取指定设备到目的地址的ping结果。-i指定当前server的某个Device， address指定ping的目的地址。样例：`hccn_tool -i 0 -ping -g address 192.168.2.1`|

使用本文档过程中，还涉及到如下环境变量，具体请参见[HCCL环境变量参考](https://gitcode.com/cann/hccl/blob/master/docs/zh/user_guide/hccl_env/README.md)。

|名称|使用场景|
|--|--|
|HCCL_RDMA_TC、HCCL_RDMA_SL|当客户对参数面网络做了自己的规划时，对各种业务流量规定了类型，优先级。通过这两个环境变量设置参数面集合通信流量在网络上的流量类型和优先级，以适配客户网络流量规划的要求。|
|HCCL_RDMA_RETRY_CNT、HCCL_RDMA_TIMEOUT|分别对应RDMA硬件重试次数和重试超时时间。设置太大导致对网络异常反应不敏感，不能感知到网络故障。设置太小则容易造成网络闪断直接造成业务中断，不能被网卡硬件屏蔽。用户可根据自身网络情况设置合适的值。例如，可以根据大部分闪断的时间范围进行配置。推荐按照如下公式进行配置，以减少网络抖动带来的影响。HCCL_RDMA_TIMEOUT=log2(pull kv超时时间 * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096)，向上取整。当pull kv超时时间和HCCL_RDMA_RETRY_CNT都等于默认值时，HCCL_RDMA_TIMEOUT建议配置成15。|
|HCCL_INTRA_ROCE_ENABLE|用于配置Server内是否使用RoCE环路进行多卡间的通信。|
|AUTO_USE_UC_MEMORY|控制系统是否允许算子搬移数据不经过L2 Cache的功能。使用LLM-DataDist之前，如果没有配置该环境变量，使用LLM-DataDist过程中，会将其设置为0，表示所有算子搬移数据都必须经过L2 Cache。|

### Python多版本支持（在线编译）

`hixl` Python包的核心是pybind11扩展模块，预编译产物仅在与构建版本一致的Python下可直接加载。使用其他Python 3.9及以上版本时，首次`import hixl`会基于随包源码自动在线编译并缓存（首次编译约10~20秒，实测x86约13秒、arm约16秒；二次导入命中缓存约0.2秒），后续导入直接复用缓存。

在线编译需要以下环境依赖（仅非预编译版本的首次导入需要）：

|依赖|说明|
|--|--|
|系统cmake|执行`apt install cmake`或`yum install cmake`安装（pip安装的cmake无效）|
|C++编译器与make|执行`apt install build-essential`或`yum install gcc-c++ make`安装；缺失时cmake configure/build阶段报错，按错误提示安装即可|
|Python开发头文件|执行`apt install python3-dev`或`yum install python3-devel`安装|
|pybind11（pip包，版本不低于2.13.6）|执行`python -m pip install "pybind11>=2.13.6"`安装|

此外须已安装HIXL软件包（含配套CANN环境，提供`hixl`公共头文件与运行库）。

编译产物缓存位置：包目录可写时写回`site-packages/hixl/`，随包升级自动失效并重建；`--pylocal y`安装形态下升级/卸载脚本会自动清理写回产物、元数据与旧包遗留文件；默认`pip --user`等自装形态卸载时按Python生态惯例可能残留（同`__pycache__`），可手动删除`hixl`目录。包目录只读时缓存于`~/.cache/cann/hixl/`（run包升级/卸载自动清理，也可手动整目录删除，下次导入时自动重新编译）。

多进程首次导入并发时自动互斥，同一时间只有一个进程执行编译，其余进程等待后直接复用编译结果。

## 完整样例参考

本示例是以transformers的LLAMA模型为样例，主要展示PD分离前后的脚本变化点，提供如何从非分离脚本改为PD分离脚本的一个参考，PD分离脚本示例请单击[Gitee](https://gitee.com/ascend/torchair/tree/master#version_match)，根据版本配套表下载对应版本的sample包，从“npu\_tuned\_model/llm/llama/benchmark/pd\_separate”目录中获取样例。样例中将全量模型和增量模型进行分离，部署到不同集群节点上执行。

如上分离脚本在整个推理流程中是如何被服务层调度的，请参考如下步骤。

1. 当用户请求触发时，服务层将用户请求调度到全量的集群上，执行全量脚本的推理。将增量脚本需要的信息传输到增量脚本的执行节点上。此时全量集群节点可接收服务层下发的新的用户请求。
2. 增量集群接收全量集群对应的请求信息，拉取对应请求的KV Cache（在全量集群节点上已经计算好的），同时按照增量模型的batch大小进行组batch操作，执行增量推理。
3. 当增量集群上有请求推理完成空出对应batch位置时，再接收全量集群发过来的新的请求，并重复执行2和3。
4. 全量集群重复执行1，增量集群重复执行2和3，直到业务结束，全量和增量集群退出。

更多代码样例，请单击[Gitcode](https://gitcode.com/cann/hixl)，选择配套版本，从“examples/python”目录中获取样例。
