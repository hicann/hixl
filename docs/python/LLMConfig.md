# LLMConfig

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## LLMConfig构造函数

**函数功能**

构造LLMConfig，调用init接口需要传入一个配置项字典，为了简化配置，可以通过此类来构造该配置项字典。

Decode和Prompt可以双向拉取Cache。该模式下的相关接口包括：enable\_cache\_manager、mem\_pool\_cfg和listen\_ip\_info。

**函数原型**

```
__init__()
```

**参数说明**

无

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
```

**返回值**

返回LLMConfig的实例。

**约束说明**

无

## generate\_options

**函数功能**

生成配置项字典。

**函数原型**

```
generate_options()
```

**参数说明**

无

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
...
engine_options = llm_config.generate_options()
```

**返回值**

返回配置项字典。

**约束说明**

无

## device\_id

**函数功能**

设置当前进程Device ID，对应底层ge.exec.deviceId配置项。

**函数原型**

```
device_id(device_id)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| device_id | Union[int, List[int], Tuple[int]] | 设置当前进程的Device ID。当前只支持配置一个 |

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.device_id = 0
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## sync\_kv\_timeout

**函数功能**

配置pull\_cache、pull\_blocks、push\_blocks、push\_cache接口的超时时间，对应底层llm.SyncKvCacheWaitTime配置项。

**函数原型**

```
sync_kv_timeout(sync_kv_timeout)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| sync_kv_timeout | int或str | 同步kv超时时间，单位：ms。不配置默认为1000ms。 |

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.sync_kv_timeout = 1000
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## ge\_options

**函数功能**

配置额外的GE配置项。

**函数原型**

```
ge_options(ge_options)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| ge_options | dict[str, str] | 配置GE配置项。<br>其中ge.flowGraphMemMaxSize比较重要，表示所有KV cache占用的最大内存，如果设置的过大，会压缩模型的可用内存，需根据实际情况指定。 |

**调用示例**

```
from llm_datadist import LLMConfig
ge_options = {
    "ge.flowGraphMemMaxSize": "4106127360"
}
llm_config = LLMConfig()
llm_config.ge_options = ge_options
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## listen\_ip\_info

**函数功能**

设置集群侦听信息，对应底层llm.listenIpInfo配置项。

**函数原型**

```
listen_ip_info(listen_ip_info)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| listen_ip_info | str | 设置为Host侧的IP地址和端口，支持配置为一个，例如："192.168.1.1:26000" |

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.listen_ip_info = "192.168.1.1:26000"
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## enable\_cache\_manager

**函数功能**

配置是否开启CacheManager模式，对应底层llm.EnableCacheManager配置项。

**函数原型**

```
enable_cache_manager(self, enable_cache_manager: bool)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| enable_cache_manager | bool | 是否开启CacheManager模式。需配置为开启。<br><br>  - True：开启。<br>  - False：不开启，不配置默认为不开启。 |

Ascend 950PR/Ascend 950DT场景下，不支持配置为False。

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## enable\_remote\_cache\_accessible

**函数功能**

配置是否开启远端Cache可直接访问功能。

开启该Option后，会在本地缓存远端Cache元数据（索引，内存地址等），从而加速Pull流程。但也会引入相关约束，详见“约束说明”。

**函数原型**

```
enable_remote_cache_accessible(self, enable_remote_cache_accessible: bool)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| enable_remote_cache_accessible | bool | 是否开启远端Cache可直接访问功能。取值如下。<br><br>  - True：开启<br>  - False：不开启<br><br>默认为不开启。<br>不开启该option时，Atlas A3 训练系列产品/Atlas A3 推理系列产品仅支持RDMA传输协议。建议开启该option，以支持更多类型的传输协议。 |

不开启该option时，Atlas A3 训练系列产品/Atlas A3 推理系列产品仅支持RDMA传输协议。建议开启该option，以支持更多类型的传输协议。
<br>Ascend 950PR/Ascend 950DT场景下，不支持配置为False。

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_remote_cache_accessible = True
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

索引的更新只在第一次Pull时触发，对端在第一次Pull之后的Cache操作对本端不可见。用户需要保证Cache的有效性。所以该Option更适用于PA的场景，因为该场景下Cache只会在用户脚本初始化阶段分配/注册，而不会频繁改变。

## rdma\_traffic\_class

**函数功能**

用于配置RDMA网卡的traffic class。

**函数原型**

```
rdma_traffic_class(self, rdma_traffic_class: int)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| rdma_traffic_class | int | 用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。 |

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)。

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_traffic_class = 100
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无。

## rdma\_service\_level

**函数功能**

用于配置RDMA网卡的service level。

**函数原型**

```
rdma_service_level(self, rdma_service_level: int)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| rdma_service_level | int | 用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。<br>取值范围为[0, 7]，默认值为4。 |

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)。

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_service_level = 2
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无。

## local\_comm\_res

**函数功能**

用于配置本地通信资源。

**函数原型**

```
local_comm_res(local_comm_res)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| local_comm_res | str | 配置本地通信资源信息，格式是json的字符串。仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。若未配置enable_cache_manager和enable_remote_cache_accessible参数，在配置了当前option后，这两个参数默认为True。

Atlas A2 训练系列产品/Atlas A2 推理系列产品场景下，仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。ranktable具体信息请参见[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。配置示例如下。

```
{
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                },
            ],
            "server_id": "xxxx"
        }
    ],
    "status": "completed",
    "version": "1.0"
}
```

该字段可以配置为空，示例如下。

```
local_comm_res = ""
```

Atlas A3 训练系列产品/Atlas A3 推理系列产品场景下，仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。ranktable具体信息请参见[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。配置示例如下。

```
{
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                },
            ],
            "server_id": "xxxx"
        }
    ],
    "status": "completed",
    "version": "1.0"
}
```

该字段可以配置为空，示例如下。

```
local_comm_res = ""
```

Ascend 950PR/Ascend 950DT场景的配置格式参考[gitcode](https://gitcode.com/cann/hixl/issues/38)，同时需要使能transfer_backend为hixl传输后端。不支持配置为空。

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.local_comm_res = '''{
    "status": "completed",
    "version": "1.0",
    "server_list": [
        {
            "server_id": "node_0",
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                }
            ]
        }
    ]
}'''
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

配置了该option后，存在如下约束。

- 调用link\_clusters或unlink\_clusters时，如果local\_comm\_res不为空，则不需要配置“clusters”参数中的“append\_local\_ip\_info”信息。如果local\_comm\_res为空，则需要配置“clusters”参数中的“append\_local\_ip\_info”信息。
- 当前不支持enable\_cache\_manager和enable\_remote\_cache\_accessible配置为“False”的场景。

## link\_total\_time

**函数功能**

用于配置HCCL建链失败的总超时时间。

**函数原型**

```
link_total_time(self, link_total_time: int)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| link_total_time | int | 用于配置HCCL的建链超时时间，单位是秒，在总超时时间内会根据重试次数来进行重试。<br>取值范围为[0, 2^32-1], 默认值为0，可不配置，不配置自动传入0；传入0后会自行读取HCCL_CONNECT_TIMEOUT环境变量，默认120s。 |

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_total_time = 20
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无。

## link\_retry\_count

**函数功能**

用于配置HCCL建链失败的重试次数。

**函数原型**

```
link_retry_count(link_retry_count)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| link_retry_count | int | 用于配置HCCL建链失败的重试次数，单位是次，在总超时时间内会根据重试次数来进行重试。<br>取值范围为[1, 100]，默认值为1，可不配置，不配置自动传入1。 |

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_retry_count = 2
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## transfer\_backend

**函数功能**

用于配置LLM-DataDist使用的传输后端。

**函数原型**

```
transfer_backend(transfer_backend)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| transfer_backend | str | 取值为“hixl”，指定hixl作为传输引擎后端。 |

**调用示例**

```
ffrom llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.transfer_backend = "hixl"
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

- 初始化option需指定listen_ip_info，当配置使用hixl传输后端时，每个传输端既可作为Client也可以作为Server。
- 与对端发起传输前需要调用link_clusters发起建链。
