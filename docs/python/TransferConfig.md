# TransferConfig

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## 函数功能

构造TransferConfig。

## 函数原型

```
__init__(dst_cluster_id: int, dst_addrs: List[int], src_layer_range: Optional[range] = None, src_batch_index: int = 0)
```

## 参数说明

| 参数名 | 数据类型 | 取值说明 |
| --- | --- | --- |
| dst_cluster_id | int | 目的Cache所在实例的cluster_id。 |
| dst_addrs | List[int] | 目的Cache中各tensor的内存地址。如果目的Cache为非PA场景，且需要传输到的batch_index非0，则此处需要将dst_addrs偏移到实际地址。 |
| src_layer_range | Optional[range] | 本地要传输的层的范围，step只支持为1，默认为None，表示传输所有层。 |
| src_batch_index | int | 本地cache的batch下标。当源Cache为非PA场景时可以设置。 |

## 调用示例

```
from llm_datadist import TransferConfig
TransferConfig(1, dst_addrs, range(0, 3), 1)
```

## 返回值

正常情况下返回TransferConfig的实例。

参数错误可能抛出TypeError或ValueError。

src\_layer\_range不合法会抛出LLMException。

## 约束说明

目标地址列表中地址的个数需要为需要传输的层数的2倍。
