# LLM-DataDist接口

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## LlmDataDist构造函数

**函数功能**

创建LLM-DataDist对象。

**函数原型**

```
LlmDataDist(uint64_t cluster_id, LlmRole role)
```

 **参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| cluster_id | 输入 | 集群ID。LlmDataDist标识，在所有参与建链的范围内需要确保唯一。 |
| role | 输入 | 类型是LlmRole，该参数只用于标识当前角色，对传输过程无影响。 |

**返回值**

无

**异常处理**

无

**约束说明**

无

## \~LlmDataDist\(\)

**函数功能**

LLM-DataDist对象析构函数。

**函数原型**

```
~LlmDataDist()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无

## Initialize

**函数功能**

初始化LLM-DataDist。

**函数原型**

```
Status Initialize(const std::map<AscendString, AscendString> &options)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| options | 输入 | 初始化参数值。具体请参考表1。 |

**表 1**  options

| 参数名 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | 可选 | 配置当前option表示LLM-DataDist是Server，不配置表示Client。<br>当LLM-DataDist是Server时，需配置Host侧的IP地址和端口。<br>配置示例：如"192.168.1.1:26000"，不支持传入多个IP地址和端口。 |
| OPTION_DEVICE_ID | 必选 | 设置当前进程的Device ID，如"0"，不支持单进程多卡场景。 |
| OPTION_SYNC_CACHE_WAIT_TIME | 可选 | kv相关操作的超时时间，单位：ms。不配置默认为1000ms。相关接口如下。<br><br>  - PullKvCache<br>  - PullKvBlocks<br>  - PushKvCache<br>  - PushKvBlocks |
| OPTION_LOCAL_COMM_RES | 可选 | 配置本地通信资源信息，格式是json格式的字符串。配置方法如下：<br>仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。该option可以不配置或配置为空串，为空将自动生成相关信息。该方法适用于如下型号：<br><br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品<br><br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品<br><br>Ascend 950PR/Ascend 950DT场景下，配置格式参考[gitcode](https://gitcode.com/cann/hixl/issues/38)，同时需要使能OPTION_TRANSFER_BACKEND为hixl传输后端。该option必选，配置为空不会自动生成相关信息。 |
| OPTION_TRANSFER_BACKEND | 可选 | 配置LLM-DataDist使用的传输后端引擎，当前支持配置的后端为“hixl”。hixl传输后端使用方法如下：<br><br>- 初始化option需指定OPTION_LISTEN_IP_INFO：当配置使用hixl传输后端时，每个传输端既可作为client也可以作为server。<br><br>- 与对端发起传输前需要调用LinkLlmClusters发起建链。 |

如上表格中ranktable具体信息请参见[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- LLM\_PARAM\_INVALID：参数错误
- 其他：失败

**异常处理**

无

**约束说明**

需要和Finalize配对使用，初始化成功后，任何退出前都需要调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。

## Finalize

**函数功能**

LLM-DataDist的资源释放函数。

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
- 当增量集群从全量集群拉取KV的时候，需要保证全量集群在增量集群完成同步KV数据之后才调用该接口，否则会出现失败。
- 当全量集群在同步KV数据到增量集群的过程中，需要保证增量集群在全量集群完成同步KV数据之后调用该接口，否则会出现失败。
- 该接口不能和其他接口并发调用。

## SetRole

**函数功能**

设置LLM-DataDist的角色。

**函数原型**

```
Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options = {})
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| role | 输入 | 角色类型，类型为LlmRole。 |
| options | 输入 | 设置角色的参数，当前支持的参数请参见表1。(#table1987921348)。 |

**表 1**  配置项

| 配置项 | 可选/必选 | 描述 |
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | 可选 | - 当LLM-DataDist初始化是Client时，如果需要切换为Server，则配置该option为侦听的Host的IP地址和端口，配置示例："192.168.1.1:26000"。否则不需要配置。<br>  - 当LLM-DataDist初始化是Server时，若不配置该option，则表示切换为Client。若配置了该opiton，则表示当前仍是Server，如果配置的端口号与初始化不一致，以当前配置为准。配置示例："192.168.1.1:26001"。<br>  -  该配置项指定hixl传输后端时，不支持通过SetRole变更侦听端口。|

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：设置角色成功
- LLM\_PARAM\_INVALID：参数错误
- LLM\_EXIST\_LINK：存在残留链路资源
- 其他：失败

**异常处理**

无

**约束说明**

使用前需与当前DataDist的链路进行断链。

## LinkLlmClusters

**函数功能**

在LlmDataDist之间执行建链，Client端调用。

**函数原型**

```
Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| clusters | 输入 | 需要建链的集群信息。类型为ClusterInfo。需要配置ClusterInfo中的remote_cluster_id和remote_ip_infos信息，若Initialize(LlmDataDist)未指定OPTION_LOCAL_COMM_RES, 需额外配置local_ip_infos，每个ClusterInfo的local_ip_infos和remote_ip_infos仅支持配置一个ip_info。 |
| rets | 输出 | 每个cluster建链结果。 |
| timeout_in_millis | 输入 | 建链超时时间，单位ms。默认1000ms。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：只有所有clusters建链成功，接口才会返回成功。
- 其他：建链失败，需要查看rets每个cluster的建链结果。

**异常处理**

- LLM\_ALREADY\_LINK：当前的cluster已经和远端cluster建立了链接。
- LLM\_LINK\_FAILED：建链失败。

**约束说明**

- 调用该接口前，需要先在Client和Server调用Initialize接口完成初始化。
- 允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品
- 建议超时时间配置为200ms以上。如果TLS处于开启状态，建议超时时间配置为2000ms以上。查询TLS状态可以使用如下命令：

    ```
    hccn_tool [-i %d] -tls -g [host]
    ```

- 调用该接口前Client和Server需提前注册所有内存，否则建链后注册不支持远端访问。
- 容器场景若未配置OPTION\_LOCAL\_COMM\_RES或配置为空，需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn_tool，如果两者都不能满足，则需要用户将hccn_tool所在路径配置到PATH中。配置实例如下，hccn_tool_install_path表示hccn_tool所在路径。该约束支持的型号如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```

## UnlinkLlmClusters

**函数功能**

在LlmDataDist之间执行断链。

**函数原型**

```
Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000, bool force_flag = false)
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| clusters | 输入 | 需要断链的cluster信息。类型为ClusterInfo。 |
| rets | 输出 | 每个cluster断链结果。 |
| timeout_in_millis | 输入 | 断链超时时间，单位ms。 |
| force_flag | 输入 | 是否为强制断链。默认为否。<br>强制断链仅强制拆除本端链接，所以两端都要调用。<br>非强制断链在Client端发起，不存在故障时两端链路都会拆除。但在存在链路故障时还需要在Server端发起调用强制断链，耗时长。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- SUCCESS：只有所有clusters断链成功，接口才会返回成功。
- 其他：执行断链失败，需要查看rets每个cluster的断链结果。

**异常处理**

LLM\_UNLINK\_FAILED：断链失败。

**约束说明**

调用该接口前，需要先调用Initialize接口完成初始化。

## PullKvCache

**函数功能**

从远端节点拉取Cache到本地Cache。

**函数原型**

```
Status PullKvCache(const CacheIndex &src_cache_index,
                   const Cache &dst_cache,
                   uint32_t batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {})
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| src_cache_index | 输入 | 远端源Cache的索引。 |
| dst_cache | 输入 | 本地目的Cache。仅需指定调用RegisterKvCache返回的cache_id。 |
| batch_index | 输入 | 本地目的batch的下标。 |
| size | 输入 | 设置为>0的整数，表示要拉取的大小，单位字节。<br>或设置为-1，表示完整拉取。<br>默认为-1。 |
| ext_param | 输入 | 当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- LLM\_PARAM\_INVALID：参数错误
- LLM\_NOT\_YET\_LINK：与远端cluster没有建链
- LLM\_TIMEOUT：拉取超时
- LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
- 其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。

## PullKvBlocks

**函数功能**

通过block列表的方式，从远端节点拉取Cache到本地Cache。

**函数原型**

```
Status PullKvBlocks(const CacheIndex &src_cache_index,
                    const Cache &dst_cache,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {})
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| src_cache_index | 输入 | 远端源Cache的索引。 |
| dst_cache | 输入 | 本地目的Cache。仅需指定调用RegisterKvCache返回的cache_id。 |
| src_blocks | 输入 | 远端源Cache的block index列表。 |
| dst_blocks | 输入 | 本地目的Cache的block index列表。 |
| ext_param | 输入 | 当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- LLM\_PARAM\_INVALID：参数错误
- LLM\_NOT\_YET\_LINK：与远端cluster没有建链
- LLM\_TIMEOUT：拉取超时
- LLM\_KV\_CACHE\_NOT\_EXIST：远端KV Cache不存在
- 其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。

## PushKvCache

**函数功能**

推送Cache到远端节点。

**函数原型**

```
Status PushKvCache(const Cache &src_cache,
                   const CacheIndex &dst_cache_index,
                   uint32_t src_batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {});
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| src_cache | 输入 | 本地目的Cache。仅需指定调用RegisterKvCache返回的cache_id。 |
| dst_cache_index | 输入 | 远端目的Cache的索引。 |
| src_batch_index | 输入 | 本地源batch的下标。 |
| size | 输入 | 当前只支持默认值-1。 |
| ext_param | 输入 | 当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- LLM\_PARAM\_INVALID：参数错误
- LLM\_NOT\_YET\_LINK：与远端cluster没有建链
- LLM\_TIMEOUT：推送超时
- LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
- 其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。

## PushKvBlocks

**函数功能**

通过block列表的方式，推送Cache到远端节点。

**函数原型**

```
Status PushKvBlocks(const Cache &src_cache,
                    const CacheIndex &dst_cache_index,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {});
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| src_cache | 输入 | 本地目的Cache。仅需指定调用RegisterKvCache返回的cache_id。 |
| dst_cache_index | 输入 | 远端目的Cache的索引。 |
| src_blocks | 输入 | 源Cache的block index列表。 |
| dst_blocks | 输入 | 目的Cache的block index列表。 |
| ext_param | 输入 | 当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- LLM\_PARAM\_INVALID：参数错误
- LLM\_NOT\_YET\_LINK：与远端cluster没有建链
- LLM\_TIMEOUT：推送超时
- LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
- 其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。

## RegisterKvCache

**函数功能**

注册本地KV Cache内存。

**函数原型**

```
Status RegisterKvCache(const CacheDesc &cache_desc,
                       const std::vector<uint64_t> &addrs,
                       const RegisterCfg &cfg,
                       int64_t &cache_id);
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| cache_desc | 输入 | 本地Cache的描述信息。 |
| addrs | 输入 | 本地Cache的地址。地址个数不超过240。 |
| cfg | 输入 | 预留参数。 |
| cache_id | 输出 | 注册的Cache的ID。可用于后续调用传输kv接口时构造Cache。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- 其他：失败

**约束说明**

需要在Initialize接口初始化完成后调用。

最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的型号如下：

- Atlas A2 训练系列产品/Atlas A2 推理系列产品
- Atlas A3 训练系列产品/Atlas A3 推理系列产品

## UnregisterKvCache

**函数功能**

解除注册本地KV Cache内存。

**函数原型**

```
Status UnregisterKvCache(int64_t cache_id);
```

**参数说明**

| 参数名称 | 输入/输出 | 取值说明 |
| --- | --- | --- |
| cache_id | 输入 | 本地注册生成的cache ID。若指定的cache_id不存在默认返回成功。 |

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

- LLM\_SUCCESS：成功
- 其他：失败

**约束说明**

调用该接口前，需要先调用Initialize接口完成初始化。cache\_id必须为RegisterKvCache接口返回的值。
