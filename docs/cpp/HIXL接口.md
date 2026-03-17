# HIXL接口

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。针对Ascend 950PR/Ascend 950DT，不支持SendNotify和GetNotifies。

## HIXL构造函数

**函数功能**

创建HIXL对象。

**函数原型**

```
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

```
~Hixl
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

```
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| local_engine | 输入 | HIXL标识，在所有参与建链的范围内需要确保唯一。如果是ipv4，格式为host_ip:host_port或host_ip。如果是ipv6，格式为[host_ip]:host_port或[host_ip]。不建议配置为回环IP，在多个HIXL交互场景，回环IP容易冲突。<br>当设置host_port且host_port>0时代表当前HIXL作为Server端，需要对配置端口进行侦听。如果没设置host_port或者host_port<=0代表是Client，不启动侦听。 |
| options | 输入 | 初始化参数值。具体请参考如下表格。 |

**表 1**  options（Atlas A2 训练系列产品/Atlas A2 推理系列产品/Atlas A3 训练系列产品/Atlas A3 推理系列产品）

| 参数名 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_BUFFER_POOL | 可选 | 字符串取值"BufferPool"。<br>在需要使用中转buffer进行传输的场景下:<br>-  不支持使用HCCS协议进行Host To Host直传传输时。<br>- RDMA注册Host内存大小受限时。<br>- 多个小块内存传输(例如128K)需要使用中转传输提升性能时。<br>可使用此option配置中转内存池的大小，取值格式为"\$BUFFER_NUM:\$BUFFER_SIZE"，系统默认会配置为"4:8(单位MB)"，可以通过配置为"0:0"来关闭中转内存池，在有并发的场景下建议增大\$BUFFER_NUM个数, 另外，所有使用的地方需要配置相同的值。不支持该参数与"OPTION_ENABLE_USE_FABRIC_MEM"同时配置。 <br>说明：不配置该参数时，存在如下约束。<br><br>Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，仅支持D2D。<br><br>Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。 |
| OPTION_RDMA_TRAFFIC_CLASS | 可选 | 字符串取值"RdmaTrafficClass"。<br>用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。 |
| OPTION_RDMA_SERVICE_LEVEL | 可选 | 字符串取值"RdmaServiceLevel"。<br>用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0, 7]，默认值为4。 |
| OPTION_GLOBAL_RESOURCE_CONFIG | 可选 | 字符串取值"GlobalResourceConfig"。用于开启并配置全局资源配置。该参数配置示例和使用约束请参考表格下方 |
| OPTION_ENABLE_USE_FABRIC_MEM | 可选 | 字符串取值"EnableUseFabricMem"。 <br>- 0：不开启Fabric Mem模式 <br>- 1：开启Fabric Mem模式 <br><br>此option适用于需要使用HCCS进行D2RH、RH2D传输的场景。 <br><br>说明：集群场景下，该参数在所有节点需要配置为相同的值。不支持该参数与"OPTION_BUFFER_POOL"同时配置。仅支持Atlas A3 训练系列产品/Atlas A3 推理系列产品。 |
| OPTION_AUTO_CONNECT | 可选 | 字符串取值"AutoConnect"。 <br>- 0：不开启Auto Connect模式 <br>- 1：开启Auto Connect模式  <br><br>说明：<br>- 开启该选项后，可跳过建链，直接进行传输。<br>- 开启该选项后，传输发生异常或对端销毁后自动清理异常链路（对端销毁需要心跳机制来检测，心跳间隔默认10s）。 |
| OPTION_LOCAL_COMM_RES | 可选 | 配置本地通信资源信息，格式是json格式的字符串。配置方法如下：<br>仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段，ranktable具体信息请参见《HCCL集合通信库用户指南》。该option可以不配置或配置为空串，为空将自动生成相关信息。 |

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)，ranktable请参考[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。
<br>OPTION_GLOBAL_RESOURCE_CONFIG的配置示例和使用约束如下：<br>对于Fabric Mem模式（仅Atlas A3 训练系列产品/Atlas A3 推理系列产品支持），该参数配置示例如下：

```
{
    "fabric_memory.max_capacity": "128", //虚拟内存池的大小。取值范围：(0, 1024]之间的整数，默认值：64，单位TB.
    "fabric_memory.start_address": "40", //虚拟内存池起始地址。取值范围：[40, 220]之间的整数，默认值：40，单位TB.
    "fabric_memory.task_stream_num": "1", //配置Fabric Mem模式下单个任务使用的流数量。取值范围：[1, 8]之间的整数，默认值：4.
}
```

<br>对于链路池机制，该参数配置示例如下：

```
{
    "channel_pool.max_channel": "10", //最大的链路个数。取值范围：(0, 512]之间的整数，默认值：512
    "channel_pool.high_waterline": "0.3", //触发链路销毁的高水位，取值范围：（0，1）之间的小数，需要和channel_pool.low_waterline同时配置
    "channel_pool.low_waterline": "0.1" //触发链路销毁的低水位，取值范围：（0，1）之间小数，并且小于高水位
}
```

链路池工作时，实际依据链路个数判断是否进行销毁，如果当前链路个数已经达到高水位对应的链路个数，则选择（当前链路个数-低水位对应的链路个数 ）条链路进行销毁（如存在正在传输的任务，则不会销毁），再建链。相关参数计算公式如下：<br>-
高水位线对应的链路个数=max(1,static_cast<int32_t> (channel_pool.max_channel *channel_pool.high_waterline))
<br>- 低水位线对应的链路个数=max(1,static_cast<int32_t> (channel_pool.max_channel* channel_pool.low_waterline))
<br>在上述配置示例中，按照计算公式，高水位对应的链路个数=3，低水位对应的链路个数=1。每次建链前会检查当前HIXL内的链路是否达到3，如果已经达到，选择(当前链路个数-1 )条链路进行销毁（如存在正在传输的任务，则不会销毁），再建链。
<br>当启用链路池机制时，有如下注意事项：
<br>- 集群内的所有Hixl Engine都需要配置OPTION_GLOBAL_RESOURCE_CONFIG。
<br>- 当调用TransferSync或TransferAsync接口时，若不存在相关链路，将执行建链操作。
<br>- 会增加传输和建链的额外开销，可能导致性能下降。

**表 2**  options（Ascend 950PR/Ascend 950DT）
| 参数名 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_LOCAL_COMM_RES | 可选 | 配置本地通信资源信息，格式是json格式的字符串。配置格式参考：<https://gitcode.com/cann/hixl/issues/37>。配置为空不会自动生成相关信息。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**异常处理**

无

**约束说明**

1. 需要和Finalize配对使用，初始化成功后，任何退出前都需要先调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
2. 初始化前需要先调用aclrtSetDevice。

## Finalize

**函数功能**

HIXL资源清理函数。

**函数原型**

```
void Finalize()
```

**参数说明**

无

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

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

注册内存地址。用于TransferSync调用指定本地内存地址和远端内存地址，TransferSync指定的地址可以为注册的地址子集，其中本地内存地址需在当前HIXL进行注册，远端内存地址需要在远端HIXL进行注册。

**函数原型**

```
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| mem | 输入 | 需要注册的内存的描述信息。类型为MemDesc。 |
| type | 输入 | 需要注册的内存的类型。类型为MemType。 |
| mem_handle | 输出 | 注册成功返回的内存handle, 可用于内存解注册。类型为MemHandle。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- 其他：失败

**异常处理**

无

**约束说明**

- 在调用Connect与对端建链之前需要完成所有local内存的注册。
- 单进程支持注册的内存个数上限是256。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 注册Host内存需使用“aclrtMallocHost”进行申请，该接口申请的内存地址自动对齐。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 注册Device内存使用“aclrtMalloc”进行申请，如通过HCCS传输，则内存分配规则需配置为ACL\_MEM\_MALLOC\_HUGE\_ONLY。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- Ascend 950PR/Ascend 950DT场景下，使用host RoCE网卡当前不支持注册“aclrtMallocHost”申请出来的内存，可使用malloc等方式。

## DeregisterMem

**函数功能**

解注册内存。

**函数原型**

```
Status DeregisterMem(MemHandle mem_handle)
```

参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| mem_handle | 输入 | 调用RegisterMem接口注册内存返回的内存handle。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

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

```
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。remote_engine对应的HIXL需要是同一个Server。 |
| timeout_in_millis | 输入 | 建链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

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
- 允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 建议超时时间配置200ms以上。
- 调用该接口前需提前注册所有本地以及远端内存，否则建链后注册不支持远端访问。
- 容器场景需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn_tool，如果两者都不能满足，则需要用户将hccn_tool所在路径配置到PATH中。配置实例如下，hccn_tool_install_path表示hccn_tool所在路径。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```
  
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## Disconnect

**函数功能**

与远端HIXL进行断链。

**函数原型**

```
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端HIXL的唯一标识。 |
| timeout_in_millis | 输入 | 断链的超时时间，单位：ms，默认值：1000。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Initialize接口完成初始化。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## TransferSync

**函数功能**

与远端HIXL进行内存传输。

**函数原型**

```
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

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- SUCCESS：成功
- PARAM\_INVALID：参数错误
- NOT\_CONNECTED：没有与对端创建链接
- TIMEOUT：传输超时
- RESOURCE_EXHAUSTED：资源耗尽
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Connect接口完成与对端的建链或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 系统默认开启中转内存池，在开启中转内存池情况下，op\_desc中本地内存和远端内存有一个未注册就会判断为需要走中转传输模式，且没有注册过的内存判断为Host内存，用户需保证地址合法。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 在中转传输模式下，所有op\_desc的传输类型需要相同，举例：所有的op\_desc都是本地Host内存往远端Host内存写。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 在Fabric Mem传输模式下, 所有op_descs的传输类型需要相同，系统会根据第一个op_desc的内存类型判定传输方向。该约束支持的型号如下：
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

## TransferAsync

**函数功能**

与远端HIXL进行批量异步内存传输。

**函数原型**

```
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

```
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
<br>需要先调用Connect接口完成与对端的建链。
<br>或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。该约束支持的型号如下：
  <br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
  <br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 当前异步传输仅支持直传，暂不支持中转传输，默认直传。
- 在Fabric Mem传输模式下, 所有op_descs的传输类型需要相同，系统会根据第一个op_desc的内存类型判定传输方向。该约束支持的型号如下：
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

## GetTransferStatus

**函数功能**

获取异步内存传输的状态。

**函数原型**

```
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| req | 输入 | 请求的句柄，通过调用TransferAsync产生。 |
| status | 输出 | 传输状态，枚举值如下。<br><br>-  WAITING<br>-  COMPLETED<br>-  TIMEOUT（暂不支持）<br>-  FAILED |

**调用示例**

```
  //初始化客户端和服务端engine，并完成链接
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req)；
  //req是TransferAsync()的输出值，使用这个请求句柄进行传输状态查询
  Status query_status = GetTransferStatus(req, status);
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
- 异步传输时，用户自行判断是否超时，如果用户判断任务超时，建议调用Disconnect接口销毁链路，清理相关资源。
- 异步传输任务失败后，调用该接口查询的状态和接口返回状态都是FAILED。

## SendNotify

**函数功能**

向远端engine发送Notify信息。

**函数原型**

```
  Status SendNotify(const AscendString &remote_engine,
                    const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| remote_engine | 输入 | 远端Hixl的唯一标识 |
| timeout_in_millis | 输入 | 发送超时时间，单位ms。
| notify | 输入 | 要发送的Notify内容。内容中的notify_msg和name长度上限均为1024字符。 |

**调用示例**

无

**返回值**

- SUCCESS：成功
- 其他：失败

**约束说明**

- 调用该接口之前，需要先调用Connect接口完成与对端的建链。
- 该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
- 该接口不支持Ascend 950PR/Ascend 950DT。

## GetNotifies

**函数功能**

获取当前Hixl内所有Server收到的Notify信息，并清空已收到信息。

**函数原型**

```
  Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| notifies | 输入 | 存放notify信息的vector。 |

**调用示例**

无

**返回值**

- SUCCESS：成功
- 其他：失败

**约束说明**

该接口不支持Ascend 950PR/Ascend 950DT。
