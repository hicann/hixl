# TransferWithCacheKeyConfig

<!-- md-trans-meta sourceCommit=8a377b1e7a351106810552edde7e8060881afc32 translatedAt=2026-11-19T01:01:42.848Z pushedAt=2026-11-19T01:04:51.724Z nl=2 -->

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

Constructs **TransferWithCacheKeyConfig**.

## Prototype

```python
__init__(cache_key: Union[BlocksCacheKey, CacheKeyByIdAndIndex], src_layer_range: range = None, dst_layer_range: range = None, src_batch_index: int = 0)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_key | Union[BlocksCacheKey, CacheKeyByIdAndIndex] | Remote Cache index. |
| src_layer_range | range | Required local layer range to transfer. Only **step=1** is supported. |
| dst_layer_range | range | Required remote layer range to transfer. Only **step=1** is supported. |
| src_batch_index | int | Local cache batch index. Settable for a non-PA source Cache. |

## Examples

```python
from llm_datadist import TransferWithCacheKeyConfig, BlocksCacheKey
TransferWithCacheKeyConfig(BlocksCacheKey(1), range(0, 40), range(0, 40))
# Transfer local layer 1 to remote layer 0. The ranges start at different indices but both contain 1 layer.
TransferWithCacheKeyConfig(BlocksCacheKey(1), range(1, 2), range(0, 1))
```

## Return Value

Returns a **TransferWithCacheKeyConfig** instance on success.

Invalid parameters may raise **TypeError**, **ValueError**, or **LLMException**.

## Constraints

- src\_layer\_range and dst\_layer\_range must contain the same number of layers, but their start and end indices may differ. Layers are matched by their relative positions within the respective ranges.
- When **cache\_key** is **BlocksCacheKey**, **src\_batch\_index** must be 0.
