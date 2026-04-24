# LLMException

调用LLMDataDist各接口，异常场景可能抛出LLMException异常。当前该类下只有一个接口status\_code。

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## 函数功能

获取异常的错误码。错误码列表详见LLMStatusCode。

## 函数原型

```
status_code()
```

## 参数说明

无

## 调用示例

```
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
try:
    kv_cache_manager.pull_cache(cache_keys[0], cache, 0)
except LLMException as exe:
    print(exe.status_code)
```

## 返回值

返回错误码。

## 约束说明

无
