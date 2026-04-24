# Cache

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。

## Cache构造函数

构造Cache，该接口通常不需要用户调用，Cache对象由CacheManager里的allocate\_cache、allocate\_blocks\_cache、register\_cache或register\_blocks\_cache返回。

## cache\_id

**函数功能**

获取Cache的id。

**函数原型**

```
@property
cache_id() -> int
```

**参数说明**

无

**调用示例**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_id)
```

**返回值**

正常情况返回类型为Cache的id。

**约束说明**

无

## cache\_desc

**函数功能**

获取Cache描述。

**函数原型**

```
@property
cache_desc() -> CacheDesc
```

**参数说明**

无

**调用示例**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_desc.num_tensors)
```

**返回值**

正常情况返回类型为Cache的cache描述。

**约束说明**

无

## tensor\_addrs

**函数功能**

获取Cache的地址。

**函数原型**

```
@property
tensor_addrs() -> List[int]
```

**参数说明**

无

**调用示例**

```
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.tensor_addrs)
```

**返回值**

正常情况返回类型为Cache的地址。

**约束说明**

无

## create\_cpu\_cache

**函数功能**

创建cpu cache。

**函数原型**

```
create_cpu_cache(cache_desc: CacheDesc, addrs: List[int])
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_desc | CacheDesc | cache的描述。 |
| addrs | List[int] | cpu cache的地址。 |

**调用示例**

```
from llm_datadist import Cache
cpu_cache = Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs) # cpu_addrs来自创建的cpu tensors
```

**返回值**

正常情况返回类型为Cache的cpu\_cache。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

传入参数为None，会抛出AttributeError异常。

**约束说明**

无
