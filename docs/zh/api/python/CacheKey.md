# CacheKey

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&Ascend 950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：支持
<!-- end id3 -->

说明：针对Atlas A2系列产品，仅支持Atlas 800I A2推理服务器、A200I A2 Box异构组件。

## 函数功能

构造CacheKey，通常CacheManager的allocate\_cache、pull\_cache接口中作为参数类型使用。

## 函数原型

```python
__init__(*args, **kwargs)
```

## 参数说明

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cluster_id | int | cache所在远端集群id，必填。 |
| req_id | int | cache关联的req_id，必填。 |
| model_id | int | cache关联的model_id，默认为0。 |
| prefix_id | int | cache关联的公共前缀id，默认为2的64次方-1。 |

## 调用示例

```python
from llm_datadist import CacheKey
cache_key = CacheKey(0, 1, 0)
```

## 返回值

正常情况下返回CacheKey的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明

无
