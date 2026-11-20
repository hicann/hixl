# CacheKey

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.586Z pushedAt=2026-10-27T02:46:32.198Z nl=2 -->

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

Constructs **CacheKey**, typically used as a parameter type in **CacheManager.allocate\_cache** and **pull\_cache**.

## Prototype

```python
__init__(*args, **kwargs)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| cluster_id | int | Remote cluster ID containing the cache. Required. |
| req_id | int | **req_id** associated with the cache. Required. |
| model_id | int | **model_id** associated with the cache. Default: 0. |
| prefix_id | int | Shared prefix ID associated with the cache. Default: 2^64 - 1. |

## Examples

```python
from llm_datadist import CacheKey
cache_key = CacheKey(0, 1, 0)
```

## Return Value

Returns a **CacheKey** instance on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

## Constraints

None
