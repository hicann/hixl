# LLMException

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.598Z pushedAt=2026-10-27T02:46:32.367Z nl=2 -->

LLMDataDist APIs may raise **LLMException** on error. This class currently contains only **status\_code**.

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

Obtains the exception error code. For codes, see **LLMStatusCode**.

## Prototype

```python
status_code()
```

## Parameters

None

## Examples

```python
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
try:
    kv_cache_manager.pull_cache(cache_keys[0], cache, 0)
except LLMException as exc:
    print(exc.status_code)
```

## Return Value

Returns the error code.

## Constraints

None
