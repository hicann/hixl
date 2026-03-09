# CacheKey

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## 函数功能

构造CacheKey，通常CacheManager的allocate\_cache、pull\_cache接口中作为参数类型使用。

## 函数原型

```
__init__(*args, **kwargs)
```

## 参数说明

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cluster_id | int | cache所在远端集群id，必填。 |
| req_id | int | cache关联的req_id，必填。 |
| model_id | int | cache关联的model_id，默认为0。 |
| prefix_id | int | cache关联的公共前缀id，默认为2的64方-1。 |

## 调用示例

```
from llm_datadist import CacheKey
cache_key = CacheKey(0, 1, 0)
```

## 返回值

正常情况下返回CacheKey的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明

无
