# MemInfo

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## 函数功能

构造MemInfo，通常在CacheManager的remap\_registered\_memory接口中作为参数类型使用。

## 函数原型

```
__init__(mem_type: Memtype, addr: int, size: int)
```

## 参数说明

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| mem_type | MemType | 内存地址类型。 |
| addr | int | 内存地址。 |
| size | int | 内存地址对应大小，单位字节。 |

## 调用示例

```
from llm_datadist import MemInfo
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, 10)
```

## 返回值

正常情况下返回MemInfo的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明

无
