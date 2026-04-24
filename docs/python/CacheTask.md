# CacheTask

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## CacheTask构造函数

构造CacheTask，由CacheManager.transfer接口返回，表示分层传输异步任务。

## synchronize

**函数功能**

等待所有层传输完成，并获取整体执行结果。

**函数原型**

```
synchronize(timeout_in_millis: Optional[int] = None) -> LLMStatusCode
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | 等待超时时间，单位为毫秒，默认为None，表示不超时。 |

**调用示例**

```
ret = cache_task.synchronize()
```

**返回值**

正常情况下返回LLMStatusCode。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

**约束说明**

无

## get\_results

**函数功能**

等待所有层传输完成，并获取每个TransferConfig对应执行结果。

**函数原型**

```
get_results(timeout_in_millis: Optional[int] = None) -> List[LLMStatusCode]
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | 等待超时时间，单位为毫秒，默认为None，表示不超时。 |

**调用示例**

```
rets = cache_task.get_results()
```

**返回值**

正常情况下返回LLMStatusCode的列表，对应每个TransferConfig的传输结果。

如果一个TransferConfig对应的layer还没有发起过传输，则对应的返回值为None。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

**约束说明**

无
