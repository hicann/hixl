# LLMClusterInfo

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## LLMClusterInfo构造函数

**函数功能**

构造LLMClusterInfo，用于link\_clusters和unlink\_clusters接口的参数类型。

**函数原型**

```
__init__()
```

**参数说明**

无

**调用示例**

```
from llm_datadist import LLMClusterInfo
llm_cluster = LLMClusterInfo()
```

**返回值**

返回LLMClusterInfo的实例。

**约束说明**

无

## remote\_cluster\_id

**函数功能**

设置对端集群ID。

**函数原型**

```
remote_cluster_id(remote_cluster_id)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| remote_cluster_id | int | 对端集群ID。 |

**调用示例**

```
llm_cluster = LLMClusterInfo()
llm_cluster.remote_cluster_id = 1
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## append\_local\_ip\_info

**函数功能**

添加本地集群IP信息。

**函数原型**

```
append_local_ip_info(self, ip: Union[str, int], port: int)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| ip | Union[str, int] | 本地集群Host卡IP。 |
| port | int | 本地集群Host卡端口。 |

**调用示例**

```
llm_cluster = LLMClusterInfo()
llm_cluster.append_local_ip_info("1.1.1.1", 10000)
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## append\_remote\_ip\_info

**函数功能**

添加远端集群IP信息。

**函数原型**

```
append_remote_ip_info(self, ip: Union[str, int], port: int)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| ip | Union[str, int] | 设置为Host侧的IP地址。 |
| port | int | 设置为Host侧的端口。 |

**调用示例**

```
llm_cluster = LLMClusterInfo()
llm_cluster.append_remote_ip_info("1.1.1.1", 10000)
```

**返回值**

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无
