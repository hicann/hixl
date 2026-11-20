# CacheTask

<!-- md-trans-meta sourceCommit=d73359f2030a4b67c287bfa81e9552a5c19cb429 translatedAt=2026-11-19T01:01:42.809Z pushedAt=2026-11-19T01:04:51.605Z nl=2 -->

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

## CacheTask Constructor

Constructs **CacheTask**, returned by **CacheManager.transfer**, representing an asynchronous layer-by-layer transfer task.

## synchronize

**Description**

Waits for all layers to finish transferring and obtains the overall result.

**Prototype**

```python
synchronize(timeout_in_millis: Optional[int] = None) -> LLMStatusCode
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Wait timeout, in milliseconds. Default: **None** (no timeout). |

**Example**

```python
ret = cache_task.synchronize()
```

**Return Value**

Normally returns an **LLMStatusCode** value.

Raises **TypeError** or **ValueError** for an incorrect input data type.

**Constraints**

None

## get\_results

**Description**

Waits for all layers to finish transferring and obtains the execution result for each **TransferConfig**.

**Prototype**

```python
get_results(timeout_in_millis: Optional[int] = None) -> List[LLMStatusCode]
```

Elements in the returned result list may be None. For the specific conditions, see "Return Value" below.

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| timeout_in_millis | Optional[int] | Wait timeout, in milliseconds. Default: **None** (no timeout). |

**Example**

```python
rets = cache_task.get_results()
```

**Return Value**

Returns a list of results corresponding to each TransferConfig. Each element is an LLMStatusCode or None. Check whether an element is None before reading its status code.

If no transfer has started for a layer corresponding to a **TransferConfig**, its result is **None**.

Raises **TypeError** or **ValueError** for an incorrect input data type.

**Constraints**

None
