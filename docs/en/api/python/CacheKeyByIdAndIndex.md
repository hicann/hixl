# CacheKeyByIdAndIndex

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.587Z pushedAt=2026-10-27T02:46:32.212Z nl=2 -->

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

## Description

Constructs **CacheKeyByIdAndIndex**, typically used as a parameter type in **CacheManager.pull\_cache** and **push\_cache**.

## Prototype

```python
__init__(cluster_id: int, cache_id: int, batch_index: int = 0)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| cluster_id | int | Cluster ID of the node containing the cache. |
| cache_id | int | Cache ID associated with the cache. |
| batch_index | int | Cache **batch_index**. |

## Examples

```python
from llm_datadist import CacheKeyByIdAndIndex
cache_key = CacheKeyByIdAndIndex(0, 1, 0)
```

## Return Value

Returns a **CacheKeyByIdAndIndex** instance on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

## Constraints

None
