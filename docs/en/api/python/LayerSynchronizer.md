# LayerSynchronizer

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.594Z pushedAt=2026-10-27T02:46:32.401Z nl=2 -->

Abstract class that users must subclass to implement the relevant APIs. Currently contains only **synchronize\_layer**.

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

Waits for the specified model layer to finish execution. Subclass **LayerSynchronizer** and implement this API.

Called during **transfer\_cache\_async**. Successful return starts cache transfer for the current layer.

## Prototype

```python
synchronize_layer(layer_index: int, timeout_in_millis: Optional[int]) -> bool
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| layer_index | int | Layer index. |
| timeout_in_millis | Optional[int] | Timeout. Currently unsupported. |

## Examples

Not called directly by users; invoked by **CacheManager** as a callback.

## Return Value

Returns whether synchronization succeeded.

Invalid parameters may raise **TypeError** or **ValueError**.

## Constraints

None
