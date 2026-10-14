# HIXL接口

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 推理系列产品/Atlas A2 训练系列产品：支持
<!-- end id3 -->

<!-- npu="910b" id37 -->
说明：针对Atlas A2系列产品，仅支持Atlas 800I A2推理服务器、A200I A2 Box异构组件。
<!-- end id37 -->

## HIXL构造函数

**函数功能**

创建HIXL对象。

**函数原型**

```cpp
Hixl()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无

## \~Hixl\(\)

**函数功能**

HIXL对象析构函数。

**函数原型**

```cpp
~Hixl();
```

**参数说明**

无

**返回值**

无

**异常处理**

无

## Initialize

**函数功能**

初始化HIXL，在调用其他接口前需要先调用该接口。

**函数原型**

```cpp
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| local_engine | 输入 | HIXL标识，在所有参与建链的范围内需要确保唯一。如果是ipv4，格式为host_ip:host_port或host_ip。如果是ipv6，格式为[host_ip]:host_port或[host_ip]。不建议配置为回环IP，在多个HIXL交互场景，回环IP容易冲突。<br>当设置host_port且host_port>0时代表当前HIXL作为Server端，需要对配置端口进行侦听。如果没设置host_port或者host_port<=0代表是Client，不启动侦听。 |
| options | 输入 | HIXL初始化参数字典（key-value均为字符串）。支持的选项及取值见 [options选项说明](#options选项说明)。 |

<a id="options选项说明"></a>

**options选项说明**

`options` 为 key-value 字符串字典，key 为下表的"选项键"，value 均为字符串。取值与约束见后续各选项小节，各产品支持的选项见下方的"各产品支持的选项"。

**表 1** options清单

| 选项键 | 常量 | 取值格式 | 必选/可选 | 默认值 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `"EnableUseFabricMem"` | `OPTION_ENABLE_USE_FABRIC_MEM` | `"0"`/`"1"` | 可选 | 不配置（不开启） | 开启Fabric Mem模式，见 [EnableUseFabricMem](#option_enable_use_fabric_mem)。 |
| `"BufferPool"` | `OPTION_BUFFER_POOL` | `"${BUFFER_NUM}:${BUFFER_SIZE}"`，单位MB | 可选 | `"4:8"` | 中转内存池配置，见 [BufferPool](#option_buffer_pool)。 |
| `"RdmaTrafficClass"` | `OPTION_RDMA_TRAFFIC_CLASS` | 整数，[0,255]且为4的整数倍 | 可选 | `132` | RDMA网卡traffic class，见 [RDMA配置](#option_rdma)。 |
| `"RdmaServiceLevel"` | `OPTION_RDMA_SERVICE_LEVEL` | 整数，[0,7] | 可选 | `4` | RDMA网卡service level，见 [RDMA配置](#option_rdma)。 |
| `"AutoConnect"` | `OPTION_AUTO_CONNECT` | `"0"`/`"1"` | 可选 | 不配置（不开启） | 自动建链模式，见 [AutoConnect](#option_auto_connect)。 |
| `"GlobalResourceConfig"` | `OPTION_GLOBAL_RESOURCE_CONFIG` | JSON字符串 | 可选 | 不配置 | 全局资源配置，见 [GlobalResourceConfig](#option_global_resource_config)。 |
| `"LocalCommRes"` | `OPTION_LOCAL_COMM_RES` | JSON字符串 | 可选 | 不配置 | 本地通信资源，见 [LocalCommRes](#option_local_comm_res)。 |

**各产品支持的选项**

<!-- npu="910b" id56 -->
- Atlas A2系列产品：`BufferPool`、`RdmaTrafficClass`、`RdmaServiceLevel`、`AutoConnect`、`GlobalResourceConfig`、`LocalCommRes`。
<!-- end id56 -->
<!-- npu="A3" id57 -->
- Atlas A3系列产品：`EnableUseFabricMem`、`BufferPool`、`RdmaTrafficClass`、`RdmaServiceLevel`、`AutoConnect`、`GlobalResourceConfig`、`LocalCommRes`。
<!-- end id57 -->
<!-- npu="950" id58 -->
- Ascend 950PR&950DT系列产品：`RdmaTrafficClass`、`RdmaServiceLevel`、`AutoConnect`、`GlobalResourceConfig`、`LocalCommRes`。
<!-- end id58 -->

> 说明：`RdmaTrafficClass`、`RdmaServiceLevel` 仅适用于RoCE场景。

> 说明：环境变量请参考《[环境变量参考](https://gitcode.com/cann/docs/blob/master/docs/zh/env-vars/README.md)》，ranktable请参考《[HCCL集合通信库](https://gitcode.com/cann/hccl/blob/master/docs/zh/user_guide/README.md)》。

<a id="option_enable_use_fabric_mem"></a>

**EnableUseFabricMem（OPTION_ENABLE_USE_FABRIC_MEM）**

- 选项键：`"EnableUseFabricMem"`。
- 取值：`"0"`表示不开启Fabric Mem模式；`"1"`表示开启Fabric Mem模式。
- 默认值：不配置（不开启）。
- 作用：开启Fabric Mem模式，适用于需要使用HCCS进行D2RH、RH2D传输的场景。
- 约束：
  - 集群场景下，所有节点的配置值需保持一致。
  - 与 `"BufferPool"` 同时配置时，`"BufferPool"` 仅支持 `"0:0"`（关闭中转内存池）。
- 示例：

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
```

<a id="option_buffer_pool"></a>

**BufferPool（OPTION_BUFFER_POOL）**

- 选项键：`"BufferPool"`。
- 取值：`"${BUFFER_NUM}:${BUFFER_SIZE}"`，单位MB；配置为 `"0:0"` 时关闭中转内存池。
- 默认值：`"4:8"`。
- 作用：配置中转内存池并开启中转传输模式，适用于需要使用中转buffer进行传输的场景，例如RDMA注册Host内存大小受限，或多个小块内存（如128K）需要经中转提升性能。
- 约束：
  - 与 `"EnableUseFabricMem"` 同时配置时，本选项仅支持 `"0:0"`（关闭中转内存池）。
  - 所有使用该配置的位置需填写相同的值。
  - 并发场景下建议增大 `${BUFFER_NUM}`。
  <!-- npu="A3,910b" id51 -->
  - 配置为 `"0:0"` 且hcomm/toolkit版本大于等于9.1.0时，HIXL使用HixlCS能力进行建链，没有链路上限限制。
  - Atlas A2系列产品场景下，若不配置本选项，Server侧采用HCCS传输协议时仅支持D2D。
  <!-- end id51 -->
- 示例：

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_BUFFER_POOL] = "4:8";
```

<a id="option_rdma"></a>

**RDMA配置（OPTION_RDMA_TRAFFIC_CLASS / OPTION_RDMA_SERVICE_LEVEL）**

- 选项键：`"RdmaTrafficClass"`、`"RdmaServiceLevel"`。
- 取值：
  - `"RdmaTrafficClass"`：整数，取值范围[0,255]，且必须为4的整数倍。
  - `"RdmaServiceLevel"`：整数，取值范围[0,7]。
- 默认值：`"RdmaTrafficClass"` 默认 `132`；`"RdmaServiceLevel"` 默认 `4`。
- 作用：配置RDMA网卡的traffic class与service level，分别与环境变量 `HCCL_RDMA_TC`、`HCCL_RDMA_SL` 功能相同。两者同时配置时以本选项为准；仅配置一方时以配置的一方为准。
- 约束：适用于RoCE场景。
- 示例：

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "4";
```

<a id="option_auto_connect"></a>

**AutoConnect（OPTION_AUTO_CONNECT）**

- 选项键：`"AutoConnect"`。
- 取值：`"0"`表示不开启Auto Connect模式；`"1"`表示开启Auto Connect模式。
- 默认值：不配置（不开启）。
- 作用：开启后可跳过建链直接进行传输。
- 约束：传输发生异常或对端销毁后会自动清理异常链路；对端销毁依赖心跳机制检测，心跳间隔默认10s。
- 示例：

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_AUTO_CONNECT] = "1";
```

<a id="option_global_resource_config"></a>

**GlobalResourceConfig（OPTION_GLOBAL_RESOURCE_CONFIG）**

- 选项键：`"GlobalResourceConfig"`。
- 取值：JSON字符串。
- 默认值：不配置。
- 作用：配置HIXL全局资源，包含通信资源与建链、数据传输批次、Fabric Mem、异步建链/断链连接池、链路池，以及本地通信资源文件（与 [LocalCommRes](#option_local_comm_res) 配套）等，全部字段见下表。

**表 GlobalResourceConfig 字段清单**

| 字段名 | 数据类型 | 必选/可选 | 简介 |
| --- | --- | --- | --- |
| comm_resource_config.protocol_desc | 字符串或字符串数组 | 可选 | 限定可用的通信协议与通信设备位置范围，详见 [protocol_desc](#grc_protocol_desc)。 |
| comm_resource_config.listen_port | JSON数字或纯数字字符串 | 可选 | device侧网卡监听端口，详见 [listen_port](#grc_listen_port)。 |
| comm_resource_config.qos | 数字 | 可选 | 通信协议qos，详见 [qos](#grc_qos)。 |
| comm_resource_config.max_active_channels | 数字 | 可选 | CS场景下设备侧同时活跃传输通道数量，详见 [max_active_channels](#grc_max_active_channels)。 |
| comm_resource_config.multi_channel.num_workers | 数字 | 可选 | 多通道并发传输的worker数，详见 [multi_channel](#grc_multi_channel)。 |
| comm_resource_config.multi_channel.split_batch_size | 数字 | 可选 | 多通道并发传输的buffer拆分粒度，详见 [multi_channel](#grc_multi_channel)。 |
| transfer_config.max_transfer_count_per_batch | 数字或十进制数字字符串 | 可选 | 单个内部传输批次最多包含的buffer数量，详见 [max_transfer_count_per_batch](#grc_transfer_batch)。 |
| fabric_memory.max_capacity | 数字或数字字符串 | 可选 | 虚拟内存池大小，详见 [fabric_memory](#grc_fabric_memory)。 |
| fabric_memory.start_address | 数字或数字字符串 | 可选 | 虚拟内存池起始地址，详见 [fabric_memory](#grc_fabric_memory)。 |
| fabric_memory.task_stream_num | 数字或数字字符串 | 可选 | 单个任务使用的流数量，详见 [fabric_memory](#grc_fabric_memory)。 |
| fabric_memory.enable_aicpu_unfold | 布尔 | 可选 | 是否由AICPU展开，详见 [fabric_memory](#grc_fabric_memory)。 |
| connect_pool.thread_num | 数字或数字字符串 | 可选 | 连接池线程数量，详见 [connect_pool](#grc_connect_pool)。 |
| connect_pool.task_queue_capacity | 数字或数字字符串 | 可选 | 连接池任务队列容量，详见 [connect_pool](#grc_connect_pool)。 |
| channel_pool.max_channel | 数字或数字字符串 | 可选 | 最大链路个数，仅通信域模式，详见 [channel_pool](#grc_channel_pool)。 |
| channel_pool.high_waterline | 数字或数字字符串 | 可选 | 链路回收高水位阈值，仅通信域模式，详见 [channel_pool](#grc_channel_pool)。 |
| channel_pool.low_waterline | 数字或数字字符串 | 可选 | 链路回收低水位阈值，仅通信域模式，详见 [channel_pool](#grc_channel_pool)。 |
| local_comm_res_path | 字符串 | 可选 | 本地通信资源JSON文件路径，与LocalCommRes配套，详见 [local_comm_res_path](#grc_local_comm_res_path)。 |
| topo_file_path | 字符串 | 可选 | 自动生成LocalCommRes时使用的硬件拓扑JSON文件路径，详见 [topo_file_path](#grc_topo_file_path)。 |

**产品支持情况**

<!-- npu="A3" id64 -->
- `fabric_memory.*`：仅支持Atlas A3系列产品。
<!-- end id64 -->
<!-- npu="A3,910b" id65 -->
- `channel_pool.*`：仅通信域模式下支持Atlas A2系列产品、Atlas A3系列产品。
<!-- end id65 -->

**详细说明与使用示例**

**通信资源与建链（`comm_resource_config.*`）**

<a id="grc_protocol_desc"></a>

- **protocol_desc**：配置可使用的通信协议以及通信设备位置范围。UB CTP纯URMA模式可使用 `ub_ctp`，其他配置使用 `${protocol}:${placement}`。
  - 支持值：`"ub_ctp"`、`"ubmem"`、`"roce:device"`、`"hccs:device"`、`"ub_ctp:device"`、`"ub_ctp:host"`、`"uboe:device"`、`"ub_rtp:device"`、`"roce:host"`。
  - 过滤行为：配置后会对 `LocalCommRes` 中显式配置的 `endpoint_list` 和自动生成的 `endpoint_list` 按该范围进行过滤；显式配置的 `LocalCommRes` 在未配置本字段时不进行额外过滤。
  <!-- npu="950" id59 -->
  - Ascend 950PR&950DT系列产品说明：未配置该字段或仅配置 `"ub_ctp:device"` 时，自动生成Device UB资源，Host内存通过UBMEM映射到Device地址后使用Device UB链路传输；同时配置 `"ub_ctp:device"` 和 `"ub_ctp:host"` 时，自动生成Device+Host UB资源并使用纯URMA路径；配置 `"ub_ctp"` 与上述组合等价；单独配置 `"ub_ctp:host"` 时仅保留Host UB CTP Endpoint。
  <!-- end id59 -->
  - 手工配置 `LocalCommRes` 时，`"ub_ctp"` 要求同时提供Device和Host UB CTP Endpoint。
  <!-- npu="A3" id63 -->
  - `ubmem` 仅可单独书写，不支持 `ubmem:device`/`ubmem:host`；`ubmem` 仅支持超节点内传输，如果要使能跨超节点自动选择roce协议，需要同时配置为 `["ubmem", "roce:device"]`，仅支持Atlas A3系列产品。
  <!-- end id63 -->

<a id="grc_listen_port"></a>

- **listen_port**：配置device侧网卡监听端口，取值范围[1, 65535]。使用说明如下。

<!-- npu="910b" id44 -->
  - Atlas A2系列产品：未配置时，固定使用 `16666` 端口。
<!-- end id44 -->
<!-- npu="A3" id45 -->
  - Atlas A3系列产品：未配置时，固定使用 `16666` 端口。
<!-- end id45 -->
<!-- npu="950" id46 -->
  - Ascend 950PR&950DT系列产品：未配置时，由底层通信组件自动选择可用端口，HIXL自动查询实际监听端口。
<!-- end id46 -->

<a id="grc_qos"></a>

- **qos**：配置通信协议qos，取值范围[0, 7]，未配置时默认为0。

<a id="grc_max_active_channels"></a>

- **max_active_channels**：CS场景下配置设备侧同时活跃传输通道数量，取值范围[1, 8192]，未配置时默认值为128。每个active channel消耗2个Stream资源，配置值需结合当前卡形态的Stream资源上限及业务中已创建的Stream数量预留余量；不同卡形态的Stream资源上限参见《[Runtime运行时API](https://gitcode.com/cann/runtime/blob/master/docs/zh/api_ref/README.md)》中的aclrtCreateStream。超出[1, 8192]时Initialize返回参数错误。

<a id="grc_multi_channel"></a>

- **multi_channel.num_workers / multi_channel.split_batch_size**：多通道并发传输配置。
  - `num_workers`：取值范围[1, 16]，默认值1（关闭多通道）。配置后，对UBOE、UB_RTP、Host RoCE和UB_CTP协议在建链时创建N个独立CS client，同步传输和异步传输均支持多通道并发，提升小包场景带宽。worker数越大占用的线程、Stream等设备资源越多，建议不超过8。
  - `split_batch_size`：取值范围[1, 4294967295]，默认值128。当单次传输的buffer数量大于split_batch_size时启用多通道拆分，拆分后的实际worker数由num_workers和buffer数量共同决定，不超过num_workers。

使用示例：

```sh
{
    "comm_resource_config.protocol_desc": ["ub_ctp:device", "ub_ctp:host"], //可选，限定可用的通信协议与通信设备位置范围
    "comm_resource_config.listen_port": "26666", //可选，device侧网卡监听端口，取值范围[1, 65535]
    "comm_resource_config.qos": 0, //可选，通信协议qos，取值范围[0, 7]，默认0
    "comm_resource_config.max_active_channels": "128", //可选，CS场景下设备侧同时活跃传输通道数量，取值范围[1, 8192]，默认128
    "comm_resource_config.multi_channel.num_workers": 1, //可选，多通道并发传输的worker数，取值范围[1, 16]，默认1
    "comm_resource_config.multi_channel.split_batch_size": 128 //可选，多通道并发传输的buffer拆分粒度，取值范围[1, 4294967295]，默认128
}
```

**数据传输批次（`transfer_config.*`）**

<a id="grc_transfer_batch"></a>

- **max_transfer_count_per_batch**：单个内部传输批次最多包含的buffer数量，超过该值时HIXL保持原始顺序自动分批。默认值1920。HIXL全局校验范围为[1, 32766]，HCCS/FabricMem范围为[1, 1920]。FabricMem两种模式都执行[1, 1920]校验：`fabric_memory.enable_aicpu_unfold=true` 时，该值控制AICPU展开的逻辑批次和Notify边界；`false` 时，Host逐条连续提交 `aclrtMemcpyAsync`，不按该值分批或在该值边界同步。RoCE/URMA队列深度：Client的SQ/SCQ深度按 `max(64, nextPowerOfTwo(配置值 + 2))` 计算；Server的SQ/SCQ深度固定为64；RQ/RCQ不由HIXL下发，由Hcomm按协议和平台默认策略设置；队列深度的协议和硬件能力校验由Hcomm负责。CANN 9.2.0及以上版本支持该配置。

使用示例：

```sh
{
    "transfer_config.max_transfer_count_per_batch": 1920 //可选，单个内部传输批次最多包含的buffer数量，默认1920
}
```

**Fabric Mem（`fabric_memory.*`）**

<a id="grc_fabric_memory"></a>

- **max_capacity**：虚拟内存池的大小，取值范围：(0, 1024]之间的整数，默认值32，单位TB，实际可用范围由底层决定。
- **start_address**：虚拟内存池起始地址，取值范围：[0, 1024]之间的整数，默认值40，单位TB。
- **task_stream_num**：单个任务使用的流数量，取值范围：[1, 8]，默认值1；enable_aicpu_unfold为true时仅支持1。
- **enable_aicpu_unfold**：是否由AICPU展开，布尔类型，默认true。

使用示例：

```sh
{
    "fabric_memory.max_capacity": "128", //虚拟内存池的大小。取值范围：(0, 1024]之间的整数，默认值：32，单位TB，实际可用范围由底层决定
    "fabric_memory.start_address": "40", //虚拟内存池起始地址。取值范围：[0, 1024]之间的整数，默认值：40，单位TB
    "fabric_memory.task_stream_num": "1", //单个任务使用的流数量，取值范围：[1, 8]，默认值：1；enable_aicpu_unfold为true时仅支持1
    "fabric_memory.enable_aicpu_unfold": true //是否由AICPU展开，布尔类型，默认true
}
```

**异步建链/断链连接池（`connect_pool.*`）**

<a id="grc_connect_pool"></a>

- **thread_num**：连接池线程数量，取值范围：[1, 64]之间的整数，默认值2。
- **task_queue_capacity**：连接池任务队列容量，取值范围：[1, 65535]之间的整数，默认值128。

使用示例：

```sh
{
    "connect_pool.thread_num": "2", //连接池线程数量。取值范围：[1, 64]之间的整数，默认值：2
    "connect_pool.task_queue_capacity": "256" //连接池任务队列容量。取值范围：[1, 65535]之间的整数，默认值：128
}
```

**链路池（`channel_pool.*`，仅通信域模式）**

<a id="grc_channel_pool"></a>

- **max_channel**：最大链路个数，取值范围：(0, 512]之间的整数，默认值512。
- **high_waterline**：链路回收的高水位阈值，取值范围：(0, 1)之间的小数，需要和channel_pool.low_waterline同时配置。
- **low_waterline**：链路回收的低水位阈值，即回收后保留的链路数比例，取值范围：(0, 1)之间的小数，需要小于高水位、与高水位同时配置。

链路池工作时，当链路数达到高水位阈值，选取 (当前链路数 - 低水位阈值对应的链路数) 条链路进行销毁（其中正在传输的链路不会被销毁）。相关参数计算如下：

```sh
高水位线对应的链路数 = max(1, floor(channel_pool.max_channel × channel_pool.high_waterline))
低水位线对应的链路数 = max(1, floor(channel_pool.max_channel × channel_pool.low_waterline))
```

在以下配置示例中，高水位对应的链路数=3，低水位对应的链路数=1。每次建链前检查当前链路数是否已达到3条，若已达到，则选取 (当前链路数 - 1) 条链路进行销毁（其中正在传输的链路不会被销毁）。

使用示例：

```sh
{
    "channel_pool.max_channel": "10", //最大链路个数。取值范围：(0, 512]之间的整数，默认值：512
    "channel_pool.high_waterline": "0.3", //链路回收的高水位阈值，取值范围：(0, 1)之间的小数，需要和channel_pool.low_waterline同时配置
    "channel_pool.low_waterline": "0.1" //链路回收的低水位阈值，即回收后保留的链路数比例，取值范围：(0, 1)之间的小数，且需要小于高水位、与高水位同时配置
}
```

- 链路池约束：
  - 启用链路池机制时，集群内所有HIXL Engine均需配置本选项。
  - 调用transfer_sync或transfer_async接口时，若不存在可用链路，链路池会自动执行建链操作。
  - 链路池机制会引入额外的传输与建链开销，可能导致性能下降。
  - 链路池仅通信域模式下支持配置，HIXL_CS等其他模式下不支持。

**本地通信资源文件（与 [LocalCommRes](#option_local_comm_res) 配套）**

<a id="grc_local_comm_res_path"></a>

- **local_comm_res_path**：本地通信资源JSON文件路径，文件内容格式与 `LocalCommRes` 相同。支持绝对路径和相对路径，相对路径基于进程当前工作目录解析。目标文件必须是大小在[1字节, 1MiB]范围内的普通文件。与 `LocalCommRes` 同时配置且 `LocalCommRes` 非空时，以 `LocalCommRes` 为准。

<a id="grc_topo_file_path"></a>

- **topo_file_path**：自动生成LocalCommRes时使用的硬件拓扑JSON文件路径。不配置或配置为空串时，HIXL按mainboard_id在默认拓扑目录中查找拓扑文件；配置非空路径时使用该文件。目标文件必须是大小在[1字节, 1MiB]范围内的普通文件，文件不存在、类型非法、超出大小或解析失败则Initialize失败。已通过 `LocalCommRes` 或 `local_comm_res_path` 提供非空endpoint_list时（手动配置local_comm_res信息时），不会读取本字段，也不校验该路径。

使用示例：

```json
{
    "local_comm_res_path": "/path/to/local_comm_res.json"
}
```

```json
{
    "topo_file_path": "/path/to/topo.json"
}
```

<a id="option_local_comm_res"></a>

**LocalCommRes（OPTION_LOCAL_COMM_RES）**

- 选项键：`"LocalCommRes"`。
- 取值：JSON字符串，本地通信资源信息。
- 默认值：不配置。
- 作用：向HIXL提供本地可用的通信资源。它与 `GlobalResourceConfig` 的 `local_comm_res_path`、`topo_file_path`、`comm_resource_config.protocol_desc` 配合决定实际使用的本地通信资源，字段见下表。

本地通信资源的提供方式及优先级如下：

| 提供方式 | 配置位置 | 说明 |
| --- | --- | --- |
| 内联JSON | `options["LocalCommRes"]`（本选项） | 直接传入本地通信资源JSON。 |
| JSON文件 | `GlobalResourceConfig` 的 `local_comm_res_path` | 指定文件路径，HIXL读取文件内容，文件内容格式与本选项相同。与本选项同时配置且本选项非空时，以本选项为准。 |
| 自动生成 | 不配置以上两者 | 由HIXL自动生成，适用产品与限制见 [version](#lcr_version) 说明。 |

<a id="通信资源配置字段说明"></a>

**表 LocalCommRes 字段清单**

| 字段名 | 数据类型 | 必选/可选 | 简介 |
| --- | --- | --- | --- |
| version | 字符串 | 必选 | 版本号，决定建链方式，详见 [version](#lcr_version)。 |
| net_instance_id | 字符串 | 必选 | 当前超节点的唯一标识，详见 [net_instance_id](#lcr_net_instance_id)。 |
| server_id | 字符串 | 可选 | 本机服务器标识，详见 [server_id](#lcr_server_id)。 |
| endpoint_list | 数组 | 必选 | 可以使用的通信设备列表，详见 [endpoint_list](#lcr_endpoint_list)。 |
| endpoint_list[].protocol | 字符串 | 必选 | 通信协议，详见 [endpoint_list](#lcr_endpoint_list)。 |
| endpoint_list[].comm_id | 字符串 | 必选 | 通信标识，详见 [endpoint_list](#lcr_endpoint_list)。 |
| endpoint_list[].placement | 字符串 | 必选 | 通信设备位置，详见 [endpoint_list](#lcr_endpoint_list)。 |
| endpoint_list[].plane | 字符串 | 可选 | 通信设备平面，详见 [endpoint_list](#lcr_endpoint_list)。 |
| endpoint_list[].dst_eid | 字符串 | 可选 | 与当前通信设备连接的对端通信设备的${eid}，详见 [endpoint_list](#lcr_endpoint_list)。 |

**详细说明与使用示例**

<a id="lcr_version"></a>

- **version**：版本号，决定建链方式。支持以下配置方式：
  <!-- npu="A3,910b" id62 -->
  - `"1.0"` 或 `"1.2"`（ranktable格式）：使用集合通信的通信域方式进行建链，建议单卡建链数量不超过512。仅需配置ranktable中当前llm datadist所使用的Device信息，无需配置 `server_count` 和 `rank_id` 字段。
  <!-- end id62 -->
  - `"1.3"`（推荐）：使用HixlCS能力进行建链，没有链路上限限制。仅配置 `version` 字段即可，其余字段自动生成。需要HDK版本大于等于25.5.0且toolkit包版本大于等于9.1.0。

自动生成的适用产品与限制：

<!-- npu="A3,910b" id52 -->
- Atlas A2系列产品、Atlas A3系列产品：不配置或配置为空串时由HIXL自动生成，使用集合通信的通信域方式进行建链。由于Device侧Stream资源有限且建链占用内存，建议单卡建链数量不超过512。
<!-- end id52 -->
<!-- npu="950" id53 -->
- Ascend 950PR&950DT系列产品：不配置或配置为空串时由HIXL自动生成。自动生成的注意事项见下方"Ascend 950PR&950DT系列产品注意事项"。
<!-- end id53 -->

<a id="lcr_net_instance_id"></a>

- **net_instance_id**：当前超节点的唯一标识，每个超节点唯一即可。

<a id="lcr_server_id"></a>

- **server_id**：本机服务器标识，用于判断传输任务是否跨机。该字段用于判断H2H传输是否出框，仅用于 protocol_desc 为 ub_ctp:host 的场景；用户可自行配置server_id，如果不配置该选项，当 protocol_desc 中包含 `ub_ctp:host` 选项时，会自动生成server_id。

<a id="lcr_endpoint_list"></a>

- **endpoint_list**：可以使用的通信设备列表，其元素字段如下。
  - `protocol`：通信协议，支持 `"roce"`、`"ub_ctp"`、`"uboe"`、`"ub_rtp"`。
  - `comm_id`：通信标识。protocol为ub_ctp/ub_rtp时填${eid}；protocol为roce时填ipv4/ipv6网卡地址；protocol为uboe时填device uboe网卡ip地址。
  - `placement`：通信设备位置，支持 `"host"`、`"device"`。
  - `plane`：通信设备平面（可选），protocol为ub_ctp时，设备区分平面则填写，每个平面唯一（如 `"plane-a"`/`"plane-b"`）。
  - `dst_eid`：与当前通信设备连接的对端通信设备的${eid}（可选），protocol为ub_ctp时，存在full-mesh直连对端则填写对端${eid}。
  - 与 `protocol_desc` 的配合：`GlobalResourceConfig` 的 `comm_resource_config.protocol_desc` 会按协议与placement范围过滤本列表（内联配置和自动生成的均会过滤）。

<!-- npu="950" id54 -->
**Ascend 950PR&950DT系列产品注意事项**

1. 自动生成LocalCommRes要求LCNE版本不低于 `LCNE: UBM_2.0.0.B011`（可前往1213前台执行 `dis startup` 查看LCNE版本信息），HDK版本不低于 `25.1.RC1.B108`（可通过 `npu-smi info` 查看HDK版本信息）。
2. 目前仅UB场景支持自动生成 `net_instance_id` 与 `endpoint_list`。如需自行配置LocalCommRes，可使用工具辅助生成指定NPU的信息，详见 [scripts/tools/hixl_tool/readme.md](../../../../scripts/tools/hixl_tool/readme.md)；如仅需生成单个NPU的Device侧UB通信边，也可使用简易工具lcrgen，详见 [scripts/tools/lcrgen/README.md](../../../../scripts/tools/lcrgen/README.md)。
3. UB场景下，若 `endpoint_list` 仅配置placement为device的UB endpoint，则仅支持Device地址的注册和传输；若仅配置placement为host的UB endpoint，则仅支持Host地址的注册和传输。需要同时使用Device和Host地址时，需同时配置对应placement的UB endpoint。
<!-- end id54 -->

**配置样例**

UB——最小配置（仅配置 `version` 字段，其他字段自动生成）：

```json
{
    "version": "1.3"
}
```

UB——完整配置：

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "server_id": "server_0",
  "endpoint_list": [
    {
      "protocol": "ub_ctp",
      "comm_id": "00000000007f020000100000df149001",
      "placement": "host",
      "dst_eid": "00000000007f030000100000df141c01"
    }
  ]
}
```

ROCE：

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "roce",
      "comm_id": "192.168.100.100",
      "placement": "host"
    }
  ]
}
```

UBOE：

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "uboe",
      "comm_id": "192.168.100.123",
      "placement": "device"
    }
  ]
}
```

UB_RTP：

```json
{
  "version": "1.3",
  "net_instance_id": "superpod_1",
  "endpoint_list": [
    {
      "protocol": "ub_rtp",
      "comm_id": "0000000000ff0a80000000000a140200",
      "placement": "device"
    }
  ]
}
```

> **说明：** 以上配置样例中的具体值仅为格式参考示例，实际使用时必须从当前环境上查询真实的通信资源配置信息进行替换，直接拷贝样例值将导致通信失败。

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**异常处理**

无

**约束说明**

1. 需要和Finalize配对使用，初始化成功后，任何退出前都需要先调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
2. 初始化前需要先调用aclrtSetDevice。
3. 初始化成功后重复调用Initialize属于幂等操作，直接返回SUCCESS，不会重复创建引擎或重新监听端口，参数变化也不会生效；如需使用新参数重新初始化，需先调用Finalize。

## Finalize

**函数功能**

HIXL资源清理函数。

**函数原型**

```cpp
void Finalize()
```

**参数说明**

无

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

无

**异常处理**

无

**约束说明**

- 需要和Initialize配对使用。
- 建议在调用Finalize前，链路进行断链以及对注册的内存进行解注册。
- Server需要等所有Client完成断链后调用，如果Server提前退出，Client断链以及数据传输过程会发生报错。
- 当Client需要操作Server端地址进行远端读写，Server端需要等Client完成远端读写之后才调用该接口，否则会出现失败。
- 该接口不能和其他接口并发调用。

## RegisterMem

**函数功能**

注册内存地址。用于TransferSync调用指定本地内存地址和远端内存地址，TransferSync指定的地址可以为注册的地址子集，其中本地内存地址需在当前HIXL进行注册，远端内存地址需要在远端HIXL进行注册。对同一内存区域（相同addr和相同len）重复调用RegisterMem，将返回SUCCESS并返回与首次注册相同的mem_handle，不会创建新的底层资源。

**函数原型**

```cpp
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| mem | 输入 | 需要注册的内存的描述信息。类型为MemDesc。 |
| type | 输入 | 需要注册的内存的类型。类型为MemType。 |
| mem_handle | 输出 | 注册成功返回的内存handle, 可用于内存解注册。类型为MemHandle。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**异常处理**

无

**约束说明**

- 在调用Connect与对端建链之前需要完成所有local内存的注册。
- 建议单个Hixl实例注册的内存个数不超过4K个。注册数量过多可能存在device OOM风险；同时注册个数越多，建链耗时越长，过多易出现建链超时问题；需用户根据业务场景自行管控内存注册数量和大小。
<!-- npu="A3,910b" id8 -->
- 最大注册50GB的Device内存。当HDK版本低于25.5时，最大注册20GB的Host内存；当HDK版本大于等于25.5时，最大注册1TB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的型号如下：
  <!-- npu="910b" id9 -->
  - Atlas A2系列产品
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3系列产品
  <!-- end id10 -->
<!-- end id8 -->
  <!-- npu="A3,910b" id11 -->
- 注册Host内存需使用“aclrtMallocHost”进行申请，该接口申请的内存地址自动对齐。该约束支持的型号如下：
  <!-- npu="910b" id12 -->
  - Atlas A2系列产品
  <!-- end id12 -->
  <!-- npu="A3" id13 -->
  - Atlas A3系列产品
  <!-- end id13 -->
<!-- end id11 -->
<!-- npu="A3" id35 -->
- FabricMem场景的Host内存存在以下约束：HDK 25.5不支持`aclrtMemRetainAllocationHandle`，必须使用ADXL的`AdxlEngine::MallocMem`申请，并使用`AdxlEngine::FreeMem`释放；HDK 26.0及以上版本可以直接使用ACL接口管理。该约束支持的型号如下：
  <!-- npu="A3" id36 -->
  - Atlas A3系列产品
  <!-- end id36 -->
<!-- end id35 -->
- 注册Device内存使用“aclrtMalloc”进行申请，如通过HCCS传输，则内存分配规则需配置为ACL\_MEM\_MALLOC\_HUGE\_ONLY。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
<!-- npu="950" id14 -->
- Ascend 950PR&950DT系列产品场景下，使用host RoCE网卡当前不支持注册“aclrtMallocHost”申请出来的内存，可使用malloc等方式。
<!-- end id14 -->

## DeregisterMem

**函数功能**

解注册内存。对同一mem_handle重复调用DeregisterMem，第一次会正确释放资源，后续调用返回SUCCESS但不执行实际操作。传入未通过RegisterMem获取的handle（非null）时返回SUCCESS但不执行实际操作；传入nullptr时返回PARAM_INVALID。

**函数原型**

```cpp
Status DeregisterMem(MemHandle mem_handle)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| mem_handle | 输入 | 调用RegisterMem接口注册内存返回的内存handle。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败。

**异常处理**

无

**约束说明**

- 调用该接口前需要先调用Disconnect将所有链路进行断链，确保所有内存不再使用。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## Connect

**函数功能**

与远端HIXL进行建链。

**函数原型**

```cpp
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| timeout_in_millis | 输入 | 建链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- TIMEOUT：建链超时
- ALREADY\_CONNECTED：重复建链
- 其他：失败

**异常处理**

无。

**约束说明**

- 需要在Client和Server的Initialize接口初始化完成后调用。
  <!-- npu="A3,910b" id15 -->
- 当OPTION_LOCAL_COMM_RES配置为空、version为"1.0"或"1.2"时，使用集合通信的通信域方式进行建链，允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。该约束支持的型号如下：
  <!-- npu="910b" id16 -->
  - Atlas A2系列产品
  <!-- end id16 -->
  <!-- npu="A3" id17 -->
  - Atlas A3系列产品
  <!-- end id17 -->
  <!-- end id15 -->
- 当OPTION_LOCAL_COMM_RES配置version为"1.3"时（推荐使用，需要HDK版本大于等于25.5.0且toolkit包版本大于等于9.1.0），使用HixlCS能力进行建链，没有链路上限限制。
- 建议超时时间配置200ms以上。
- 调用该接口前需提前注册所有本地以及远端内存，否则建链后注册不支持远端访问。
  <!-- npu="A3,910b" id18 -->
- 容器场景需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn_tool，如果两者都不能满足，则需要用户将hccn_tool所在路径配置到PATH中。配置实例如下，hccn_tool_install_path表示hccn_tool所在路径。该约束支持的型号如下：
  <!-- npu="910b" id19 -->
  - Atlas A2系列产品
  <!-- end id19 -->
  <!-- npu="A3" id20 -->
  - Atlas A3系列产品
  <!-- end id20 -->

  ```sh
  export PATH=$PATH:{hccn_tool_install_path}
  ```
  <!-- end id18 -->

<!-- npu="A3,910b" id38 -->
- 对于使用Device RoCE场景，同一通信集群内Device RoCE地址配置需保持一致，不支持IPv6-only节点与IPv4/IPv6双栈节点混合接入。该约束支持的型号如下：
  <!-- npu="910b" id39 -->
  - Atlas A2系列产品
  <!-- end id39 -->
  <!-- npu="A3" id40 -->
  - Atlas A3系列产品
  <!-- end id40 -->
<!-- end id38 -->

<!-- npu="A3,910b" id41 -->
- 对于使用Device RoCE场景，HIXL在获取RoCE设备IP地址时遵循以下原则：
  - **IPv4优先**：同时存在IPv4和IPv6地址时，优先使用IPv4地址。
  - **IPv6兜底**：当设备无IPv4地址但存在IPv6地址时，自动使用IPv6地址。
  - **无需额外配置**：用户无需新增配置项或显式指定地址族，HIXL自动从hccn.conf或hccn_tool获取设备IP地址。
  该约束支持的型号如下：
  <!-- npu="910b" id42 -->
  - Atlas A2系列产品
  <!-- end id42 -->
  <!-- npu="A3" id43 -->
  - Atlas A3系列产品
  <!-- end id43 -->
<!-- end id41 -->

- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## Disconnect

**函数功能**

与远端HIXL进行断链。

**函数原型**

```cpp
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| timeout_in_millis | 输入 | 断链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Initialize接口完成初始化。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## ConnectAsync

**函数功能**

与远端HIXL进行异步建链。

**函数原型**

```cpp
Status ConnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| timeout_in_millis | 输入 | 建链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout_in_millis <= 0）
- RESOURCE\_EXHAUSTED：任务队列已满
- 其他：失败

**异常处理**

无。

**约束说明**

- 继承Connect接口的所有约束。
- 线程池线程数量和任务队列长度通过Hixl的Initialize接口进行配置。
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- ConnectAsync/DisconnectAsync接口不与Connect/Disconnect接口混用。
- 对同一remote_engine下发多个任务时，按下发顺序执行；不同remote_engine的任务允许并发执行。获取的任务状态为最新下发任务的状态。

## DisconnectAsync

**函数功能**

与远端HIXL进行异步断链。

**函数原型**

```cpp
Status DisconnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| timeout_in_millis | 输入 | 断链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout_in_millis <= 0）
- RESOURCE\_EXHAUSTED：任务队列已满
- 其他：失败

**约束说明**

- 继承Disconnect接口的所有约束。
- 线程池线程数量和任务队列长度通过Hixl的Initialize接口进行配置。
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- ConnectAsync/DisconnectAsync接口不与Connect/Disconnect接口混用。
- 对同一remote_engine下发多个任务时，按下发顺序执行；不同remote_engine的任务允许并发执行。获取的任务状态为最新下发任务的状态。

## GetAsyncConnectStatus（查询指定连接状态）

**函数功能**

获取指定异步连接状态。

**函数原型**

```cpp
Status GetAsyncConnectStatus(const AscendString &remote_engine, AsyncConnectStatus &status)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| status | 输出 | 异步连接状态，枚举值如下。<br><br>-  NOT_CONNECT 未连接<br>-  CONNECT_PENDING 建链待执行<br>-  CONNECTING 建链执行中<br>-  CONNECTED 建链成功<br>-  CONNECT_FAILED 建链失败<br>-  DISCONNECT_PENDING 断链待执行<br>-  DISCONNECTING 断链执行中 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Initialize接口完成初始化。
- 接口的返回值仅表示接口调用是否成功，异步建链/断链任务状态由输出参数表示。

## GetAsyncConnectStatus（查询全部连接状态）

**函数功能**

获取全部异步连接状态。

**函数原型**

```cpp
Status GetAsyncConnectStatus(std::map<AscendString, AsyncConnectStatus> &statuses)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| statuses | 输出 | 异步连接状态，枚举值如下。<br><br>-  NOT_CONNECT 未连接<br>-  CONNECT_PENDING 建链待执行<br>-  CONNECTING 建链执行中<br>-  CONNECTED 建链成功<br>-  CONNECT_FAILED 建链失败<br>-  DISCONNECT_PENDING 断链待执行<br>-  DISCONNECTING 断链执行中 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Initialize接口完成初始化。
- 接口的返回值仅表示接口调用是否成功，异步建链/断链任务状态由输出参数表示。

## TransferSync

**函数功能**

与远端HIXL进行内存传输。

**函数原型**

```cpp
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| operation | 输入 | 将远端内存读到本地或者将本地内存写到远端。 |
| op_descs | 输入 | 批量操作的本地以及远端地址。 |
| timeout_in_millis | 输入 | 传输的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- TIMEOUT：传输超时
- RESOURCE_EXHAUSTED：资源耗尽
- 其他：失败

**约束说明**

  <!-- npu="A3,910b" id21 -->
- 调用该接口之前，需要先调用Connect接口完成与对端的建链或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。该约束支持的型号如下：
  <!-- npu="910b" id22 -->
  - Atlas A2系列产品
  <!-- end id22 -->
  <!-- npu="A3" id23 -->
  - Atlas A3系列产品
  <!-- end id23 -->
  <!-- end id21 -->
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
  <!-- npu="A3,910b" id24 -->
- 系统默认开启中转内存池，在开启中转内存池情况下，op\_desc中本地内存和远端内存有一个未注册就会判断为需要走中转传输模式，且没有注册过的内存判断为Host内存，用户需保证地址合法。该约束支持的型号如下：
  <!-- npu="910b" id25 -->
  - Atlas A2系列产品
  <!-- end id25 -->
  <!-- npu="A3" id26 -->
  - Atlas A3系列产品
  <!-- end id26 -->
  <!-- end id24 -->
  <!-- npu="A3,910b" id27 -->
- 在中转传输模式下，所有op\_desc的传输类型需要相同，举例：所有的op\_desc都是本地Host内存往远端Host内存写。该约束支持的型号如下：
  <!-- npu="910b" id28 -->
  - Atlas A2系列产品
  <!-- end id28 -->
  <!-- npu="A3" id29 -->
  - Atlas A3系列产品
  <!-- end id29 -->
  <!-- end id27 -->
  <!-- npu="A3" id30 -->
- 在Fabric Mem传输模式下, 所有op_descs的传输类型需要相同，系统会根据第一个op_desc的内存类型判定传输方向。该约束支持的型号如下：
  - Atlas A3系列产品
  <!-- end id30 -->

## TransferAsync

**函数功能**

与远端HIXL进行批量异步内存传输。

**函数原型**

```cpp
  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| operation | 输入 | 将远端内存读到本地或者将本地内存写到远端。 |
| op_descs | 输入 | 批量操作的本地以及远端地址。 |
| optional_args | 输入 | 可选参数（预留）。 |
| req | 输出 | 请求的句柄，用户查询传输的请求状态。 |

**调用示例**

```cpp
  //初始化客户端和服务端engine，并完成链接
  client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
```

**返回值**

- SUCCESS：成功
- NOT\_CONNECTED：没有与对端创建链接
- RESOURCE_EXHAUSTED：资源耗尽
- 其他：失败

**约束说明**

- 调用该接口之前，存在如下约束：
  - 需要先调用Connect接口完成与对端的建链。
  <!-- npu="A3,910b" id31 -->
  - 或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。该约束支持的型号如下：
    <!-- npu="910b" id32 -->
    - Atlas A2系列产品
    <!-- end id32 -->
    <!-- npu="A3" id33 -->
    - Atlas A3系列产品
    <!-- end id33 -->
  <!-- end id31 -->
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 当前异步传输仅支持直传，暂不支持中转传输，默认直传。
  <!-- npu="A3" id34 -->
- 在Fabric Mem传输模式下, 所有op_descs的传输类型需要相同，系统会根据第一个op_desc的内存类型判定传输方向。该约束支持的型号如下：
  - Atlas A3系列产品
  <!-- end id34 -->

## GetTransferStatus（查询指定传输请求）

**函数功能**

获取异步内存传输的状态。

**函数原型**

```cpp
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| req | 输入 | 请求的句柄，通过调用TransferAsync产生。 |
| status | 输出 | 传输状态，枚举值如下。<br><br>-  WAITING<br>-  COMPLETED<br>-  TIMEOUT（暂不支持）<br>-  FAILED |

**调用示例**

```cpp
  //初始化客户端和服务端engine，并完成链接
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  //req是TransferAsync()的输出值，使用这个请求句柄进行传输状态查询
  Status query_status = client_engine.GetTransferStatus(req, status);
  //对传输状态进行检查，判断传输是否完成
  ...
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Connect接口完成与对端的建链。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 在调用TransferAsync接口进行异步传输后，需要使用该接口查询对应请求状态，如果查询状态是COMPLETED或FAILED，将释放相关资源。该场景下不支持再次查询。
- 异步传输时，用户自行判断是否超时，如果用户判断任务超时，需要调用Disconnect接口销毁链路，清理相关资源。

## GetTransferStatus（查询全部传输请求）

**函数功能**

获取所有异步内存传输的状态。

**函数原型**

```cpp
  Status GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| args | 输入 | 获取所有异步传输请求的状态参数 |
| results | 输出 | 所有异步传输请求的状态 |

**调用示例**

```cpp
  //初始化客户端和服务端engine，并完成链接
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  //req是TransferAsync()的输出值，使用这个请求句柄进行传输状态查询
  GetTransferStatusArgs args = { .max_query_count = 4, .skip_waiting = true };
  std::vector<TransferResult> results;
  Status query_status = client_engine.GetTransferStatus(args, results);
  //对传输状态进行检查，判断传输是否完成
  ...
```

**返回值**

- SUCCESS：成功
- UNSUPPORTED: Hixl初始化的options未配置LocalCommRes的version为1.3且未配置GlobalResourceConfig的comm_resource_config.protocol_desc包含uboe:device或ub_rtp:device时，不支持通过该接口查询
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Connect接口完成与对端的建链。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 在调用TransferAsync接口进行异步传输后，需要使用该接口查询所有请求状态，如果某请求状态是COMPLETED或FAILED，将释放相关资源。该场景下再次查询将不再返回该请求状态。
- 异步传输时，用户自行判断是否超时，如果用户判断任务超时，建议调用Disconnect接口销毁链路，清理相关资源。

## SendNotify

**函数功能**

向远端engine发送Notify信息。

**函数原型**

```cpp
  Status SendNotify(const AscendString &remote_engine,
                    const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端Hixl的唯一标识。 |
| notify | 输入 | 要发送的Notify内容。内容中的notify_msg和name长度上限均为1024字节，由AscendString.GetLength()按存储字节数计量；多字节编码的字符数不等于此长度。 |
| timeout_in_millis | 输入 | 发送超时时间，单位：ms，默认值：1000。 |

**调用示例**

```cpp
// client_engine已完成初始化，并已与remote_engine建链。
NotifyDesc notify;
notify.name = AscendString("cache_ready");
notify.notify_msg = AscendString("block_0");
Status ret = client_engine.SendNotify(remote_engine, notify, 1000);
if (ret != SUCCESS) {
  // 处理发送失败。
}
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout_in_millis <= 0，或notify.name/notify_msg长度超过1024）
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Connect接口完成与对端的建链。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 每条链路中最多存在4096条Notify，需要确保远端Hixl及时调用GetNotifies接口消费Notify防止触发上限导致发送失败。

## GetNotifies

**函数功能**

获取当前Hixl内所有Server收到的Notify信息，并清空已收到信息。

**函数原型**

```cpp
  Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| notifies | 输出 | 存放notify信息的vector。 |

**调用示例**

```cpp
std::vector<NotifyDesc> notifies;
Status ret = server_engine.GetNotifies(notifies);
if (ret != SUCCESS) {
  // 处理获取失败。
}
```

**返回值**

- SUCCESS：成功
- 其他：失败

**约束说明**

无

## GetCapability

**函数功能**

查询库能力特性。上层可在Initialize之前调用该接口，探测当前Hixl库是否支持特定能力（如Auto Connect、Client/Server通信），避免硬编码默认值或与旧版.so不兼容。

**函数原型**

```cpp
static Status GetCapability(FeatureType feature_type, int32_t &value)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| feature_type | 输入 | 特性类型，取值参见 FeatureType。 |
| value | 输出 | 特性支持情况。1表示支持（FEATURE_SUPPORTED），0表示不支持（FEATURE_NOT_SUPPORTED）。 |

**调用示例**

```cpp
int32_t value = FEATURE_NOT_SUPPORTED;
Status ret = Hixl::GetCapability(AUTO_CONNECT, value);
if (ret != SUCCESS) {
  // 处理查询失败。
}
bool supports_auto_connect = (value == FEATURE_SUPPORTED);
```

**返回值**

- SUCCESS：成功。未知或不支持的特性类型时，`value` 为 `FEATURE_NOT_SUPPORTED`
- PARAM_INVALID：参数非法（feature_type为负数）

**约束说明**

- 无需调用Initialize即可调用。
- 静态方法，不依赖Hixl实例。
