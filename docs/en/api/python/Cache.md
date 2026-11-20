# Cache

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.584Z pushedAt=2026-10-27T02:46:32.176Z nl=2 -->

## Applicable Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 inference products/Atlas A2 training products: Supported
<!-- end id3 -->

<!-- npu="910b" id4 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id4 -->

## Cache Constructor

Constructs **Cache**. Users typically do not call this API directly. **Cache** objects are returned by **CacheManager.allocate\_cache**, **allocate\_blocks\_cache**, **register\_cache**, or **register\_blocks\_cache**.

## cache\_id

**Description**

Obtains the **Cache** ID.

**Prototype**

```python
@property
cache_id() -> int
```

**Parameters**

None

**Example**

```python
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_id)
```

**Return Value**

Returns the **Cache** ID on success.

**Constraints**

None

## cache\_desc

**Description**

Obtains the **Cache** description.

**Prototype**

```python
@property
cache_desc() -> CacheDesc
```

**Parameters**

None

**Example**

```python
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.cache_desc.num_tensors)
```

**Return Value**

Returns the **Cache** description on success.

**Constraints**

None

## tensor\_addrs

**Description**

Obtains the **Cache** addresses.

**Prototype**

```python
@property
tensor_addrs() -> List[int]
```

**Parameters**

None

**Example**

```python
...
kv_cache = cache_manager.allocate_cache(cache_desc, cache_keys)
print(kv_cache.tensor_addrs)
```

**Return Value**

Returns the **Cache** addresses on success.

**Constraints**

None

## create\_cpu\_cache

**Description**

Creates a CPU cache.

**Prototype**

```python
create_cpu_cache(cache_desc: CacheDesc, addrs: List[int])
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| addrs | List[int] | CPU cache addresses. |

**Example**

```python
from llm_datadist import Cache
cpu_cache = Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs) # cpu_addrs comes from the created CPU tensors.
```

**Return Value**

Returns **cpu\_cache** of type **Cache** on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **AttributeError** if the input parameter is **None**.

**Constraints**

None
