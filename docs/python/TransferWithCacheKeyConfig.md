# TransferWithCacheKeyConfig

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## 函数功能

构造TransferWithCacheKeyConfig。

## 函数原型

```
__init__(cache_key: Union[BlocksCacheKey, CacheKeyByIdAndIndex], src_layer_range: range = None, dst_layer_range: range = None, src_batch_index: int = 0)
```

## 参数说明

| 参数名 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_key | Union[BlocksCacheKey, CacheKeyByIdAndIndex] | 目的Cache所在实例的cluster_id。 |
| src_layer_range | range | 必选参数，本地要传输的层的范围，step只支持为1。 |
| dst_layer_range | range | 必选参数，远端要传输的层的范围，step只支持为1。 |
| src_batch_index | int | 本地cache的batch下标。当源Cache为非PA场景时可以设置。 |

## 调用示例

```
from llm_datadist import TransferWithCacheKeyConfig
TransferWithCacheKeyConfig(BlocksCacheKey(1), range(0, 40), range(0, 40))
```

## 返回值

正常情况下返回TransferWithCacheKeyConfig的实例。

参数错误可能抛出TypeError、ValueError或LLMException。

## 约束说明

- src\_layer\_range表示范围需等于dst\_layer\_range表示范围。
- cache\_key为BlocksCacheKey时，src\_batch\_index只能为0。
