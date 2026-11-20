# TransferConfig

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.604Z pushedAt=2026-10-27T02:46:32.473Z nl=2 -->

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

Constructs **TransferConfig**.

## Prototype

```python
__init__(dst_cluster_id: int, dst_addrs: List[int], src_layer_range: Optional[range] = None, src_batch_index: int = 0)
```

## Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| dst_cluster_id | int | **cluster_id** of the instance containing the destination Cache. |
| dst_addrs | List[int] | Memory addresses of destination Cache tensors. For a non-PA destination Cache with **batch_index** other than 0, offset **dst_addrs** to the actual addresses. |
| src_layer_range | Optional[range] | Local layer range to transfer. Only **step=1** is supported. Default: **None**, all layers. |
| src_batch_index | int | Local cache batch index. Settable for a non-PA source Cache. |

## Examples

```python
from llm_datadist import TransferConfig
TransferConfig(1, dst_addrs, range(0, 3), 1)
```

## Return Value

Returns a **TransferConfig** instance on success.

Invalid parameters may raise **TypeError** or **ValueError**.

Invalid **src\_layer\_range** raises **LLMException**.

## Constraints

The destination address count must be 2 times the layer count to transfer.
