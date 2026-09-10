# HIXL接口

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 推理系列产品/Atlas A2 训练系列产品：支持
<!-- end id3 -->

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## Hixl构造函数

**函数功能**

创建Hixl对象。

**函数原型**

```python
__init__()
```

**参数说明**

无

**调用示例**

```python
import hixl
engine = hixl.Hixl()
```

**返回值**

正常情况下返回Hixl实例。

**约束说明**

- 如果Hixl对象在销毁前未调用finalize，析构函数会自动调用finalize进行资源清理。但仍建议显式调用finalize以确保资源按预期释放。

## initialize

**函数功能**

初始化HIXL，在调用其他接口前需要先调用该接口。

**函数原型**

```python
initialize(local_engine: str, options: Dict[str, str] = {}) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| local_engine | str | HIXL标识，在所有参与建链的范围内需要确保唯一。如果是ipv4，格式为host\_ip:host\_port或host\_ip。如果是ipv6，格式为[host\_ip]:host\_port或[host\_ip]。不建议配置为回环IP，在多个HIXL交互场景，回环IP容易冲突。<br>当设置host\_port且host\_port>0时代表当前HIXL作为Server端，需要对配置端口进行侦听。如果没设置host\_port或者host\_port<=0代表是Client，不启动侦听。 |
| options | Dict[str, str] | 初始化参数值。具体请参考如下表格。 |

<!-- npu="A3,910b" id5 -->
**表 1**  options（Atlas A2 训练系列产品/Atlas A2 推理系列产品/Atlas A3 训练系列产品/Atlas A3 推理系列产品）

| 参数名 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_ENABLE_USE_FABRIC_MEM | 可选 | 字符串取值"EnableUseFabricMem"。 <br>- 0：不开启Fabric Mem模式 <br>- 1：开启Fabric Mem模式 <br><br>此option适用于需要使用HCCS进行D2RH、RH2D传输的场景。 <br><br>说明：集群场景下，该参数在所有节点需要配置为相同的值。不支持该参数与"OPTION_BUFFER_POOL"同时配置。仅支持Atlas A3 训练系列产品/Atlas A3 推理系列产品。 |
| OPTION_BUFFER_POOL | 可选 | 字符串取值"BufferPool"。<br>在需要使用中转buffer进行传输的场景下:<br>- RDMA注册Host内存大小受限时。<br>- 多个小块内存传输(例如128K)需要使用中转传输提升性能时。<br>可使用此option配置中转内存池的大小，取值格式为"${BUFFER_NUM}:${BUFFER_SIZE}"，系统默认会配置为"4:8(单位MB)"，可以通过配置为"0:0"来关闭中转内存池，在有并发的场景下建议增大${BUFFER_NUM}个数, 另外，所有使用的地方需要配置相同的值。不支持该参数与"OPTION_ENABLE_USE_FABRIC_MEM"同时配置。 <br>说明：不配置该参数时，存在如下约束。<br><br>Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，仅支持D2D。 |
| OPTION_RDMA_TRAFFIC_CLASS | 可选 | 字符串取值"RdmaTrafficClass"。<br>用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。 |
| OPTION_RDMA_SERVICE_LEVEL | 可选 | 字符串取值"RdmaServiceLevel"。<br>用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0, 7]，默认值为4。 |
| OPTION_GLOBAL_RESOURCE_CONFIG | 可选 | 字符串取值"GlobalResourceConfig"。用于开启并配置全局资源配置。该参数配置示例和使用约束请参考表格下方 |
| OPTION_AUTO_CONNECT | 可选 | 字符串取值"AutoConnect"。 <br>- 0：不开启Auto Connect模式 <br>- 1：开启Auto Connect模式  <br><br>说明：<br>- 开启该选项后，可跳过建链，直接进行传输。<br>- 开启该选项后，传输发生异常或对端销毁后自动清理异常链路（对端销毁需要心跳机制来检测，心跳间隔默认10s）。 |
| OPTION_LOCAL_COMM_RES | 可选 | 配置本地通信资源信息，格式是json格式的字符串。<br>- 不配置或配置为空串：将自动生成相关信息，使用集合通信的通信域方式进行建链。由于Device侧Stream资源有限，且建链会占用内存，建议单卡建链数量不超过512。也可通过OPTION_GLOBAL_RESOURCE_CONFIG中的local_comm_res_path指定本地通信资源JSON文件路径，由HIXL读取文件内容作为本地通信资源；两者同时配置且本option非空时，以本option为准。<br>  说明：当OPTION_BUFFER_POOL（或adxl.BufferPool）配置为"0:0"（关闭中转内存池）且hcomm/toolkit版本大于等于9.1.0时，将使用HixlCS能力进行建链，没有链路上限限制。<br>- 配置version为"1.0"或"1.2"的ranktable格式：使用集合通信的通信域方式进行建链。由于Device侧Stream资源有限，且建链会占用内存，建议单卡建链数量不超过512。仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段，ranktable具体信息请参见《[HCCL集合通信库](https://gitcode.com/cann/hccl/blob/9.2.0/docs/zh/user_guide/README.md)》。<br>- 配置version为"1.3"（推荐使用，需要HDK版本大于等于25.5.0且toolkit包版本大于等于9.1.0）：使用HixlCS能力进行建链，没有链路上限限制。配置格式参考[通信资源配置字段说明](#通信资源配置字段说明)，仅配置version字段即可，其他字段将自动生成。 |

如上表格中的环境变量请参考《[环境变量参考](https://gitcode.com/cann/docs/blob/9.2.0/docs/zh/env-vars/README.md)》，ranktable请参考《[HCCL集合通信库](https://gitcode.com/cann/hccl/blob/9.2.0/docs/zh/user_guide/README.md)》。

<!-- end id5 -->
<!-- npu="A3,910b" id7 -->
OPTION_LOCAL_COMM_RES配置为"1.3"版本的配置示例如下：

- 最小配置（仅配置version字段，其他字段自动生成）：

```json
{
    "version": "1.3"
}
```

- 完整配置示例（手动指定通信资源信息）：

```json
{
    "version": "1.3",
    "net_instance_id": "superpod1_1",
    "endpoint_list": [
        {
            "protocol": "roce",
            "comm_id": "10.10.10.1",
            "placement": "device"
        }
    ]
}
```

> **说明：** 推荐使用最小配置方式，系统会自动生成本地通信资源信息。如需手动指定，endpoint_list中各字段的含义请参考[通信资源配置字段说明](#通信资源配置字段说明)。

OPTION_GLOBAL_RESOURCE_CONFIG的配置示例和使用约束如下：

<!-- npu="A3" id6 -->
对于Fabric Mem模式（仅Atlas A3 训练系列产品/Atlas A3 推理系列产品支持），该参数配置示例如下：

```sh
{
    "fabric_memory.max_capacity": "128", //虚拟内存池的大小。取值范围：(0, 1024]之间的整数，默认值：32，单位TB，实际可用范围由底层决定
    "fabric_memory.start_address": "40", //虚拟内存池起始地址。取值范围：[0, 1024]之间的整数，默认值：40，单位TB
    "fabric_memory.task_stream_num": "1", //单个任务使用的流数量，取值范围：[1, 8]，默认值：1；enable_aicpu_unfold为true时仅支持1
    "fabric_memory.enable_aicpu_unfold": true //是否由AICPU展开FabricMem，布尔类型，默认true
}
```
<!-- end id6 -->

对于异步建链/断链机制，该参数配置示例如下：

```sh
{
    "connect_pool.thread_num":"2", // 连接池线程数量。取值范围：[1, 64]之间的整数，默认值：2
    "connect_pool.task_queue_capacity":"256" // 连接池任务队列容量。取值范围：[1, 65535]之间的整数，默认值：128
}
```

device侧网卡默认监听端口为16666，如果在多个进程使用同一个网卡的场景，可以做如下配置：

```sh
{
    "comm_resource_config.listen_port": "26666", //可选，取值范围：[1, 65535]之间的整数。不配置时，自动生成ranktable不携带device_port字段
    "comm_resource_config.max_active_channels": "128" //可选，CS场景下配置设备侧同时活跃传输通道数量。取值范围：[1, 8192]，默认值：128，每个active channel消耗2个Stream资源
}
```

通过文件路径配置本地通信资源时，可以做如下配置：

```json
{
    "local_comm_res_path": "/path/to/local_comm_res.json"
}
```

`local_comm_res_path`支持绝对路径和相对路径，相对路径基于进程当前工作目录解析。目标文件必须是大小在[1字节, 1MiB]范围内的普通文件，文件内容格式与OPTION_LOCAL_COMM_RES相同。与OPTION_LOCAL_COMM_RES同时配置且option非空时，以OPTION_LOCAL_COMM_RES为准。

对于链路池机制，该参数配置示例如下：

```sh
{
    "channel_pool.max_channel": "10", //最大链路个数。取值范围：(0, 512]之间的整数，默认值：512
    "channel_pool.high_waterline": "0.3", //链路回收的高水位阈值，取值范围：(0, 1)之间的小数，需要和channel_pool.low_waterline同时配置
    "channel_pool.low_waterline": "0.1" //链路回收的低水位阈值，即回收后保留的链路数比例，取值范围：(0, 1)之间的小数，且需要小于高水位、与高水位同时配置
}
```

链路池工作时，当链路数达到高水位阈值，选取 (当前链路数 - 低水位阈值对应的链路数) 条链路进行销毁（其中正在传输的链路不会被销毁）。相关参数计算如下：

```sh
高水位线对应的链路数 = max(1, floor(channel_pool.max_channel × channel_pool.high_waterline))
低水位线对应的链路数 = max(1, floor(channel_pool.max_channel × channel_pool.low_waterline))
```

在上述配置示例中，高水位对应的链路数=3，低水位对应的链路数=1。每次建链前检查当前链路数是否已达到3条，若已达到，则选取 (当前链路数 - 1) 条链路进行销毁（其中正在传输的链路不会被销毁）。

启用链路池机制需注意：

- 集群内的所有Hixl Engine均需配置OPTION_GLOBAL_RESOURCE_CONFIG。
- 调用transfer_sync或transfer_async接口时，若不存在可用链路，链路池会自动执行建链操作。
- 链路池机制会引入额外的传输与建链开销，可能导致性能下降。
<!-- end id7 -->

<!-- npu="950" id4 -->
**表 2**  options（Ascend 950PR/Ascend 950DT）

| 参数名 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_LOCAL_COMM_RES | 可选 | 配置本地通信资源信息，格式是 json 格式的字符串。配置格式参考[通信资源配置字段说明](#通信资源配置字段说明)，配置为空不会自动生成相关信息。也可通过OPTION_GLOBAL_RESOURCE_CONFIG中的local_comm_res_path指定本地通信资源JSON文件路径，由HIXL读取文件内容作为本地通信资源。OPTION_LOCAL_COMM_RES配置为非空字符串或OPTION_GLOBAL_RESOURCE_CONFIG中的local_comm_res_path配置为有效文件路径，两者至少配置一项。两者同时配置且本option非空时，以本option为准。配置样例见下方[配置样例](#配置样例)<br/>**注意：<br/>1、以上配置样例中的具体值仅为格式参考示例，实际使用时必须从当前环境上查询真实的通信资源配置信息进行替换，直接拷贝样例值将导致通信失败。<br/>2、自动生成localcommres能力需要用户使用root权限调用hixl接口，且要求LCNE版本不低LCNE: UBM_2.0.0.B011，可前往1213前台执行dis startup查看LCNE版本信息；HDK版本不低于25.1.RC1.B108，可通过npu-smi info来查看HDK版本信息。<br/>3、目前仅UB场景支持自动生成net_instance_id与endpoint_list，如果用户想要自行配置localcommres信息，可以使用工具来辅助生成指定npu的localcommres信息，具体使用方法详见[scripts/tools/lcrgen/README.md](../../../../scripts/tools/lcrgen/README.md)。<br/>4、UB场景下，如果endpoint_list仅配置placement为device的UB endpoint，则仅支持Device地址的注册和传输；如果endpoint_list仅配置placement为host的UB endpoint，则仅支持Host地址的注册和传输。需要同时使用Device和Host地址时，需同时配置对应placement的UB endpoint。** |
| OPTION_GLOBAL_RESOURCE_CONFIG | 可选 | 字符串取值 "GlobalResourceConfig"。用于开启并配置全局资源，格式为 json 格式的字符串，字段说明参考[全局资源配置字段说明](#全局资源配置字段说明)。 |
| OPTION_AUTO_CONNECT | 可选 | 字符串取值 "AutoConnect"。取值：0 — 不开启 Auto Connect 模式；1 — 开启 Auto Connect 模式。说明：开启该选项后，可跳过建链，直接进行传输；开启该选项后，传输发生异常或对端销毁后自动清理异常链路（对端销毁需要心跳机制来检测，心跳间隔默认 10s）。 |
| OPTION_RDMA_TRAFFIC_CLASS | 可选 | 字符串取值"RdmaTrafficClass"。<br>用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。<br>说明：适用于Ascend 950PR/Ascend 950DT的RoCE场景。 |
| OPTION_RDMA_SERVICE_LEVEL | 可选 | 字符串取值"RdmaServiceLevel"。<br>用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0, 7]，默认值为4。<br>说明：适用于Ascend 950PR/Ascend 950DT的RoCE场景。 |
<!-- end id4 -->

<a id="配置样例"></a>**配置样例**

UB——最小配置（仅配置version字段，其他字段自动生成）

```json
{
    "version": "1.3"
}
```

UB——完整配置

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

ROCE

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

UBOE

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

UB_RTP

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

<a id="通信资源配置字段说明"></a>**通信资源配置字段说明**

| 字段名 | 数据类型 | 必选/可选 | 说明 | 支持值/填写规则 |
| --- | --- | --- | --- | --- |
| version | 字符串 | 必选 | 版本号 | "1.3"。需要HDK版本大于等于25.5.0且toolkit包版本大于等于9.1.0。 |
| net_instance_id | 字符串 | 必选 | 当前超节点的唯一标识 | 每个超节点唯一即可。 |
| server_id | 字符串 | 可选 | 当前服务器标识 | 仅用于protocol为ub_ctp且placement为host的同OS H2rH loopback判断。为空或两端不一致时不启用该判断。 |
| endpoint_list | 数组 | 必选 | 可以使用的通信设备列表 | - |
| endpoint_list[].protocol | 字符串 | 必选 | 通信协议 | "roce"/"ub_ctp"/"uboe"/"ub_rtp" |
| endpoint_list[].comm_id | 字符串 | 必选 | 通信标识 | protocol为ub_ctp/ub_rtp时填`${eid}`；protocol为roce时填ipv4/ipv6网卡地址；protocol为uboe时填device uboe网卡ip地址 |
| endpoint_list[].placement | 字符串 | 必选 | 通信设备位置 | "host"/"device" |
| endpoint_list[].plane | 字符串 | 可选 | 通信设备平面 | protocol为ub_ctp时，设备区分平面则填写，每个平面唯一（如"plane-a"/"plane-b"） |
| endpoint_list[].dst_eid | 字符串 | 可选 | 与当前通信设备连接的对端通信设备的`${eid}` | protocol为ub_ctp时，存在full-mesh直连对端则填写对端`${eid}` |

<a id="全局资源配置字段说明"></a>**全局资源配置字段说明**

| 字段名 | 数据类型 | 必选/可选 | 说明 | 支持值/填写规则 |
| --- | --- | --- | --- | --- |
| comm_resource_config.protocol_desc | 字符串或字符串数组 | 可选 | 配置可使用的通信协议以及通信设备位置范围。UB CTP纯URMA模式可使用`ub_ctp`，其他配置使用`${protocol}:${placement}` | 支持"ub_ctp"/"roce:device"/"hccs:device"/"ub_ctp:device"/"ub_ctp:host"/"uboe:device"/"ub_rtp:device"/"roce:host"。配置后会对OPTION_LOCAL_COMM_RES中显式配置的endpoint_list和自动生成的endpoint_list按该范围进行过滤。A5上未配置该字段或仅配置"ub_ctp:device"时，自动生成Device UB资源，Host内存通过UBMEM映射到Device地址后使用Device UB链路传输；同时配置"ub_ctp:device"和"ub_ctp:host"时，自动生成Device+Host UB资源并使用纯URMA路径；配置"ub_ctp"与上述组合等价。单独配置"ub_ctp:host"时仅保留Host UB CTP Endpoint。手工配置LocalCommRes时，"ub_ctp"要求同时提供Device和Host UB CTP Endpoint。显式配置的OPTION_LOCAL_COMM_RES在未配置本字段时不进行额外过滤。 |
| comm_resource_config.listen_port | JSON数字或纯数字字符串 | 可选 | 配置device侧网卡监听端口 | 取值范围为[1, 65535]。Atlas A2 训练系列产品/Atlas A2 推理系列产品、Atlas A3 训练系列产品/Atlas A3 推理系列产品上未配置时，固定使用`16666`端口；Ascend 950PR/Ascend 950DT场景未配置时，由底层通信组件自动选择可用端口，HIXL自动查询实际监听端口。 |
| comm_resource_config.qos | 数字 | 可选 | 配置通信协议qos | 当前仅支持[0-7]，当未配置的时候，默认为0。 |
| comm_resource_config.max_active_channels | 数字 | 可选 | CS场景下配置设备侧同时活跃传输通道数量 | 取值范围为[1, 8192]，未配置时默认值为128。每个active channel消耗2个Stream资源，配置值需结合当前卡形态的Stream资源上限及业务中已创建的Stream数量预留余量；不同卡形态的Stream资源上限参见CANN Runtime API [aclrtCreateStream](https://www.hiascend.com/document/detail/zh/canncommercial/latest/API/runtimeapi/aclcppdevg_03_0066.html)资料。超出[1, 8192]时initialize返回参数错误。 |
| local_comm_res_path | 字符串 | 可选 | 本地通信资源 JSON 文件路径；文件内容格式与 OPTION_LOCAL_COMM_RES 相同 | 配置文件的绝对或相对路径，相对路径基于进程当前工作目录解析。目标文件必须是大小在[1字节, 1MiB]范围内的普通文件。与 OPTION_LOCAL_COMM_RES 同时配置且 option 非空时，以 OPTION_LOCAL_COMM_RES 为准。 |

**调用示例**

请参考[样例运行](../../../../examples/python/hixl/hixl_d2rd_multiproc_sample.py)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**约束说明**

- 需要和finalize配对使用，初始化成功后，任何退出前都需要先调用finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
- 初始化前需要先调用aclrtSetDevice。
- 重复调用initialize将返回SUCCESS并忽略重复调用。

## finalize

**函数功能**

HIXL资源清理函数。

**函数原型**

```python
finalize() -> None
```

**参数说明**

无

**调用示例**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
# ... 业务逻辑 ...
engine.finalize()
```

**返回值**

无

**约束说明**

- 需要和initialize配对使用。
- 建议在调用finalize前，链路进行断链以及对注册的内存进行解注册。
- Server需要等所有Client完成断链后调用，如果Server提前退出，Client断链以及数据传输过程会发生报错。
- 当Client需要操作Server端地址进行远端读写，Server端需要等Client完成远端读写之后才调用该接口，否则会出现失败。
- 该接口不能和其他接口并发调用。

## register\_mem

**函数功能**

注册内存地址。用于transfer\_sync调用指定本地内存地址和远端内存地址，transfer\_sync指定的地址可以为注册的地址子集，其中本地内存地址需在当前HIXL进行注册，远端内存地址需要在远端HIXL进行注册。对同一内存区域（相同addr和相同len）重复调用register\_mem，将返回SUCCESS并返回与首次注册相同的mem\_handle，不会创建新的底层资源。

**函数原型**

```python
register_mem(mem_desc: MemDesc, mem_type: MemType) -> Tuple[int, int]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| mem_desc | MemDesc | 需要注册的内存的描述信息。 |
| mem_type | MemType | 需要注册的内存的类型。 |

**调用示例**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
mem_desc = hixl.MemDesc(addr=dev_addr, len=buf_size)
ret, handle = engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
```

**返回值**

返回一个元组(ret, mem\_handle)：

- ret：状态码，SUCCESS表示成功，PARAM\_INVALID表示参数错误，其他表示失败。
- mem\_handle：注册成功返回的内存handle（int类型），可用于内存解注册。

**约束说明**

- 在调用connect与对端建链之前需要完成所有local内存的注册。
- 建议单个Hixl实例注册的内存个数不超过4K个。注册数量过多可能存在device OOM风险；同时注册个数越多，建链耗时越长，过多易出现建链超时问题；需用户根据业务场景自行管控内存注册数量和大小。
<!-- npu="A3,910b" id8 -->
- 最大注册50GB的Device内存。当HDK版本低于25.5时，最大注册20GB的Host内存；当HDK版本大于等于25.5时，最大注册1TB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的型号如下：
  <!-- npu="910b" id9 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id10 -->
<!-- end id8 -->
  <!-- npu="A3,910b" id11 -->
- 注册Host内存需使用"aclrtMallocHost"进行申请，该接口申请的内存地址自动对齐。该约束支持的型号如下：
  <!-- npu="910b" id12 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id12 -->
  <!-- npu="A3" id13 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id13 -->
<!-- end id11 -->
<!-- npu="A3" id35 -->
- FabricMem场景的Host内存存在以下约束：HDK 25.5不支持`aclrtMemRetainAllocationHandle`，必须使用ADXL的`AdxlEngine::MallocMem`申请，并使用`AdxlEngine::FreeMem`释放；HDK 26.0及以上版本可以直接使用ACL接口管理。该约束支持的型号如下：
  <!-- npu="A3" id36 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id36 -->
<!-- end id35 -->
- 注册Device内存使用"aclrtMalloc"进行申请，如通过HCCS传输，则内存分配规则需配置为ACL\_MEM\_MALLOC\_HUGE\_ONLY。
<!-- npu="950" id14 -->
- Ascend 950PR/Ascend 950DT场景下，使用host RoCE网卡当前不支持注册"aclrtMallocHost"申请出来的内存，可使用malloc等方式。
<!-- end id14 -->

## deregister\_mem

**函数功能**

解注册内存。对同一mem\_handle重复调用deregister\_mem，第一次会正确释放资源，后续调用返回SUCCESS但不执行实际操作。传入未通过register\_mem获取的handle（非0）时返回SUCCESS但不执行实际操作；传入0时返回PARAM\_INVALID。

**函数原型**

```python
deregister_mem(mem_handle: int) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| mem_handle | int | 调用register\_mem接口注册内存返回的内存handle。 |

**调用示例**

```python
ret = engine.deregister_mem(handle)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**约束说明**

- 调用该接口前需要先调用disconnect将所有链路进行断链，确保所有内存不再使用。

## connect

**函数功能**

与远端HIXL进行建链。

**函数原型**

```python
connect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| timeout_in_millis | int | 建链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
ret = engine.connect("127.0.0.1:16001", timeout_in_millis=5000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- TIMEOUT：建链超时
- ALREADY\_CONNECTED：重复建链
- 其他：失败

**约束说明**

- 需要在Client和Server的initialize接口初始化完成后调用。
  <!-- npu="A3,910b" id15 -->
- 当OPTION\_LOCAL\_COMM\_RES配置为空、version为"1.0"或"1.2"时，使用集合通信的通信域方式进行建链，允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。该约束支持的型号如下：
  <!-- npu="910b" id16 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id16 -->
  <!-- npu="A3" id17 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id17 -->
  <!-- end id15 -->
- 当OPTION\_LOCAL\_COMM\_RES配置version为"1.3"时（推荐使用，需要HDK版本大于等于25.5.0且toolkit包版本大于等于9.1.0），使用HixlCS能力进行建链，没有链路上限限制。
- 建议超时时间配置200ms以上。
- 调用该接口前需提前注册所有本地以及远端内存，否则建链后注册不支持远端访问。
  <!-- npu="A3,910b" id18 -->
- 容器场景需在容器内映射"/etc/hccn.conf"文件或者确保默认路径"/usr/local/Ascend/driver/tools"下存在hccn\_tool，如果两者都不能满足，则需要用户将hccn\_tool所在路径配置到PATH中。配置实例如下，hccn\_tool\_install\_path表示hccn\_tool所在路径。该约束支持的型号如下：
  <!-- npu="910b" id19 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id19 -->
  <!-- npu="A3" id20 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id20 -->

  ```sh
  export PATH=$PATH:{hccn_tool_install_path}
  ```
  <!-- end id18 -->

- 对于使用Device RoCE场景，同一通信集群内Device RoCE地址配置需保持一致，不支持IPv6-only节点与IPv4/IPv6双栈节点混合接入。该约束支持的型号如下：
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品

## disconnect

**函数功能**

与远端HIXL进行断链。

**函数原型**

```python
disconnect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| timeout_in_millis | int | 断链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
ret = engine.disconnect("127.0.0.1:16001", timeout_in_millis=5000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用initialize接口完成初始化。

## connect\_async

**函数功能**

与远端HIXL进行异步建链。

**函数原型**

```python
connect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| timeout_in_millis | int | 建链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
ret = engine.connect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout\_in\_millis <= 0）
- RESOURCE\_EXHAUSTED：任务队列已满
- 其他：失败

**约束说明**

- 继承connect接口的所有约束。
- 线程池线程数量和任务队列长度通过Hixl的initialize接口进行配置。
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- connect\_async/disconnect\_async接口不与connect/disconnect接口混用。
- 对同一remote\_engine下发多个任务时，按下发顺序执行；不同remote\_engine的任务允许并发执行。获取的任务状态为最新下发任务的状态。

## disconnect\_async

**函数功能**

与远端HIXL进行异步断链。

**函数原型**

```python
disconnect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| timeout_in_millis | int | 断链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
ret = engine.disconnect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout\_in\_millis <= 0）
- RESOURCE\_EXHAUSTED：任务队列已满
- 其他：失败

**约束说明**

- 继承disconnect接口的所有约束。
- 线程池线程数量和任务队列长度通过Hixl的initialize接口进行配置。
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- connect\_async/disconnect\_async接口不与connect/disconnect接口混用。
- 对同一remote\_engine下发多个任务时，按下发顺序执行；不同remote\_engine的任务允许并发执行。获取的任务状态为最新下发任务的状态。

## get\_async\_connect\_status

**函数功能**

获取指定异步连接状态。

**函数原型**

```python
get_async_connect_status(remote_engine: str) -> Tuple[int, AsyncConnectStatus]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |

**调用示例**

```python
ret, status = engine.get_async_connect_status("127.0.0.1:16001")
if status == hixl.AsyncConnectStatus.CONNECTED:
    print("Connected")
```

**返回值**

返回一个元组(ret, status)：

- ret：状态码，SUCCESS表示成功，其他表示失败。
- status：异步连接状态，枚举值参见[AsyncConnectStatus](HIXL-data-structure.md#asyncconnectstatus)。

**约束说明**

- 调用该接口之前，需要先调用initialize接口完成初始化。
- 接口的返回值仅表示接口调用是否成功，异步建链/断链任务状态由输出参数表示。

## get\_all\_async\_connect\_status

**函数功能**

获取全部异步连接状态。

**函数原型**

```python
get_all_async_connect_status() -> Tuple[int, Dict[str, AsyncConnectStatus]]
```

**参数说明**

无

**调用示例**

```python
ret, statuses = engine.get_all_async_connect_status()
for remote, status in statuses.items():
    print(f"{remote}: {status}")
```

**返回值**

返回一个元组(ret, statuses)：

- ret：状态码，SUCCESS表示成功，其他表示失败。
- statuses：字典，key为远端HIXL标识（str），value为异步连接状态（AsyncConnectStatus）。

**约束说明**

- 调用该接口之前，需要先调用initialize接口完成初始化。
- 接口的返回值仅表示接口调用是否成功，异步建链/断链任务状态由输出参数表示。

## transfer\_sync

**函数功能**

与远端HIXL进行内存传输。

**函数原型**

```python
transfer_sync(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| op | TransferOp | 将远端内存读到本地或者将本地内存写到远端。 |
| op_descs | List[TransferOpDesc] | 批量操作的本地以及远端地址。 |
| timeout_in_millis | int | 传输的超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret = engine.transfer_sync("127.0.0.1:16001", hixl.TransferOp.READ, op_descs, timeout_in_millis=30000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- TIMEOUT：传输超时
- RESOURCE\_EXHAUSTED：资源耗尽
- 其他：失败

**约束说明**

  <!-- npu="A3,910b" id21 -->
- 调用该接口之前，需要先调用connect接口完成与对端的建链或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION\_GLOBAL\_RESOURCE\_CONFIG参数进行开启）。该约束支持的型号如下：
  <!-- npu="910b" id22 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id22 -->
  <!-- npu="A3" id23 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id23 -->
  <!-- end id21 -->
  <!-- npu="A3,910b" id24 -->
- 系统默认开启中转内存池，在开启中转内存池情况下，op\_desc中本地内存和远端内存有一个未注册就会判断为需要走中转传输模式，且没有注册过的内存判断为Host内存，用户需保证地址合法。该约束支持的型号如下：
  <!-- npu="910b" id25 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id25 -->
  <!-- npu="A3" id26 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id26 -->
  <!-- end id24 -->
  <!-- npu="A3,910b" id27 -->
- 在中转传输模式下，所有op\_desc的传输类型需要相同，举例：所有的op\_desc都是本地Host内存往远端Host内存写。该约束支持的型号如下：
  <!-- npu="910b" id28 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <!-- end id28 -->
  <!-- npu="A3" id29 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id29 -->
  <!-- end id27 -->
  <!-- npu="A3" id30 -->
- 在Fabric Mem传输模式下, 所有op\_descs的传输类型需要相同，系统会根据第一个op\_desc的内存类型判定传输方向。该约束支持的型号如下：
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id30 -->

## transfer\_async

**函数功能**

与远端HIXL进行批量异步内存传输。

**函数原型**

```python
transfer_async(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], args: TransferArgs = TransferArgs()) -> Tuple[int, int]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端HIXL的唯一标识。 |
| op | TransferOp | 将远端内存读到本地或者将本地内存写到远端。 |
| op_descs | List[TransferOpDesc] | 批量操作的本地以及远端地址。 |
| args | TransferArgs | 可选参数（预留）。 |

**调用示例**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret, req_id = engine.transfer_async("127.0.0.1:16001", hixl.TransferOp.WRITE, op_descs)
```

**返回值**

返回一个元组(ret, req\_id)：

- ret：状态码，SUCCESS表示成功，NOT\_CONNECTED表示没有与对端创建链接，RESOURCE\_EXHAUSTED表示资源耗尽，其他表示失败。
- req\_id：请求的句柄（int类型），用于查询传输的请求状态。

**约束说明**

- 调用该接口之前，存在如下约束：
  - 需要先调用connect接口完成与对端的建链。
  <!-- npu="A3,910b" id31 -->
  - 或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION\_GLOBAL\_RESOURCE\_CONFIG参数进行开启）。该约束支持的型号如下：
    <!-- npu="910b" id32 -->
    - Atlas A2 训练系列产品/Atlas A2 推理系列产品
    <!-- end id32 -->
    <!-- npu="A3" id33 -->
    - Atlas A3 训练系列产品/Atlas A3 推理系列产品
    <!-- end id33 -->
  <!-- end id31 -->
- 当前异步传输仅支持直传，暂不支持中转传输，默认直传。
  <!-- npu="A3" id34 -->
- 在Fabric Mem传输模式下, 所有op\_descs的传输类型需要相同，系统会根据第一个op\_desc的内存类型判定传输方向。该约束支持的型号如下：
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id34 -->

## get\_transfer\_status

**函数功能**

获取异步内存传输的状态。

**函数原型**

```python
get_transfer_status(req_id: int) -> Tuple[int, TransferStatus]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| req_id | int | 请求的句柄，通过调用transfer\_async产生。 |

**调用示例**

```python
ret, status = engine.get_transfer_status(req_id)
if status == hixl.TransferStatus.COMPLETED:
    print("Transfer completed")
```

**返回值**

返回一个元组(ret, status)：

- ret：状态码，SUCCESS表示成功，PARAM\_INVALID表示参数错误，NOT\_CONNECTED表示没有与对端创建链接，其他表示失败。
- status：传输状态，枚举值参见[TransferStatus](HIXL-data-structure.md#transferstatus)。

**约束说明**

- 调用该接口之前，需要先调用connect接口完成与对端的建链。
- 在调用transfer\_async接口进行异步传输后，需要使用该接口查询对应请求状态，如果查询状态是COMPLETED或FAILED，将释放相关资源。该场景下不支持再次查询。
- 异步传输时，用户自行判断是否超时，如果用户判断任务超时，需要调用disconnect接口销毁链路，清理相关资源。

## get\_all\_transfer\_status

**函数功能**

获取所有异步内存传输的状态。

**函数原型**

```python
get_all_transfer_status(args: GetTransferStatusArgs = GetTransferStatusArgs()) -> Tuple[int, List[TransferResult]]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| args | GetTransferStatusArgs | 获取所有异步传输请求的状态参数。 |

**调用示例**

```python
args = hixl.GetTransferStatusArgs(max_query_count=4, skip_waiting=True)
ret, results = engine.get_all_transfer_status(args)
for result in results:
    print(f"req={result.req}, status={result.status}")
```

**返回值**

返回一个元组(ret, results)：

- ret：状态码，SUCCESS表示成功，UNSUPPORTED表示Hixl初始化的options未配置LocalCommRes的version为1.3且未配置GlobalResourceConfig的comm\_resource\_config.protocol\_desc包含uboe:device或ub\_rtp:device时不支持通过该接口查询，其他表示失败。
- results：TransferResult列表，每个元素包含传输请求的状态信息。

**约束说明**

- 调用该接口之前，需要先调用connect接口完成与对端的建链。
- 在调用transfer\_async接口进行异步传输后，需要使用该接口查询所有请求状态，如果某请求状态是COMPLETED或FAILED，将释放相关资源。该场景下再次查询将不再返回该请求状态。
- 异步传输时，用户自行判断是否超时，如果用户判断任务超时，建议调用disconnect接口销毁链路，清理相关资源。

## send\_notify

**函数功能**

向远端engine发送Notify信息。

**函数原型**

```python
send_notify(remote_engine: str, notify: NotifyDesc, timeout_in_millis: int = 1000) -> int
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_engine | str | 远端Hixl的唯一标识。 |
| notify | NotifyDesc | 要发送的Notify内容。内容中的notify\_msg和name长度上限均为1024字符。 |
| timeout_in_millis | int | 发送超时时间，单位：ms，默认值：1000。 |

**调用示例**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
ret = engine.send_notify("127.0.0.1:16001", notify, timeout_in_millis=1000)
```

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误（timeout\_in\_millis <= 0，或notify.name/notify\_msg长度超过1024）
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用connect接口完成与对端的建链。
- 每条链路中最多存在4096条Notify，需要确保远端Hixl及时调用get\_notifies接口消费Notify防止触发上限导致发送失败。

## get\_notifies

**函数功能**

获取当前Hixl内所有Server收到的Notify信息，并清空已收到信息。

**函数原型**

```python
get_notifies() -> Tuple[int, List[NotifyDesc]]
```

**参数说明**

无

**调用示例**

```python
ret, notifies = engine.get_notifies()
for n in notifies:
    print(f"name={n.name}, msg={n.notify_msg}")
```

**返回值**

返回一个元组(ret, notifies)：

- ret：状态码，SUCCESS表示成功，其他表示失败。
- notifies：NotifyDesc列表。

**约束说明**

无

## get\_capability

**函数功能**

查询库能力特性。上层可在initialize之前调用该接口，探测当前Hixl库是否支持特定能力（如Auto Connect、Client/Server通信），避免硬编码默认值或与旧版.so不兼容。

**函数原型**

```python
hixl.get_capability(feature_type: FeatureType) -> Tuple[int, int]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| feature_type | FeatureType | 特性类型，取值参见[FeatureType](HIXL-data-structure.md#featuretype)。 |

**调用示例**

```python
ret, value = hixl.get_capability(hixl.FeatureType.AUTO_CONNECT)
supports_auto_connect = (value == hixl.FEATURE_SUPPORTED)
```

**返回值**

返回一个元组(ret, value)：

- ret：状态码，SUCCESS表示成功。未知或不支持的特性类型时，value为FEATURE\_NOT\_SUPPORTED。PARAM\_INVALID表示参数非法（feature\_type为负数）。
- value：特性支持情况。1表示支持（FEATURE\_SUPPORTED），0表示不支持（FEATURE\_NOT\_SUPPORTED）。

**约束说明**

- 模块级函数，不依赖Hixl实例。
