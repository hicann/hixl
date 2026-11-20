# MemInfo

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.600Z pushedAt=2026-10-27T02:46:32.413Z nl=2 -->

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

Constructs **MemInfo**, typically used as a parameter type in **CacheManager.remap\_registered\_memory**.

## Prototype

```python
__init__(mem_type: Memtype, addr: int, size: int)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| mem_type | Memtype | Memory address type. |
| addr | int | Memory address. |
| size | int | Memory region size, in bytes. |

## Examples

```python
from llm_datadist import MemInfo, Memtype
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, 10)
```

## Return Value

Returns a **MemInfo** instance on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

## Constraints

None
