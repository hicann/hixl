# CacheDesc

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## 函数功能

构造CacheDesc，通常在CacheManager的allocate\_cache接口中作为参数类型使用。

## 函数原型

```
__init__(self,
                 num_tensors: int,
                 shape: Union[Tuple[int], List[int]],
                 data_type: DataType,
                 placement: Placement = Placement.DEVICE,
                 batch_dim_index: int = 0,
                 seq_len_dim_index: int = -1,
                 kv_tensor_format: str = None)
```

## 参数说明

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| num_tensors | int | cache中tensor的个数，操作cache时，所有tensor会做同样的操作。 |
| shape | Union[Tuple[int], List[int]] | tensor的shape。 |
| data_type | DataType | tensor的data type。 |
| placement | Placement | 表示cache所在的设备类型。默认值Placement.DEVICE。 |
| batch_dim_index | int | 表示shape中batch size所在维度。默认值0，表示在第0维。 |
| seq_len_dim_index | int | 表示shape中seq_len所在维度。默认值-1，表示未配置。 |
| kv_tensor_format | str | 表示cache的format。默认不配置。 |

## 调用示例

```
from llm_datadist import CacheDesc
cache_desc = CacheDesc(80, [4, 2048, 1, 128], DataType.DT_FLOAT16)
```

## 返回值

正常情况下返回CacheDesc的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明

无
