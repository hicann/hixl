# To Be Deprecated

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.590Z pushedAt=2026-10-27T02:46:32.514Z nl=2 -->

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

## CacheManager

### allocate\_cache

**Description**

Allocates a Cache. After allocation, both **cache\_id** and **cache\_keys** reference it. Resources are freed only after all references are removed.

The **cache\_id** reference must be removed by calling **deallocate\_cache**, while **cache\_keys** references can be removed using either of the following 2 methods.

- Removed after Decode successfully calls **pull\_cache** or **push\_cache**.
- Removed when PROMPT calls **remove\_cache\_key**.

**Prototype**

```python
allocate_cache(cache_desc: CacheDesc, cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = ())
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache index. |

**Example**

```python
from llm_datadist import *
...
cache_desc = CacheDesc(1, [2, 1024 * 1024], DataType.DT_FLOAT16)
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
cache = cache_manager.allocate_cache(cache_desc, cache_keys)
```

**Return Value**

Returns a **Cache** on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if **cache\_keys** contains a **CacheKey** bound during memory allocation.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

- If **cache\_keys** is supplied and the Cache **batch size\>1**, provide the same number of **CacheKey** entries, each referencing a group of KV tensors.
- If an inference batch is not full and contains invalid **batch\_index** entries, insert placeholder **CacheKey** entries with **req\_id** set to **UINT64\_MAX**. Trailing unused **batch\_index** entries may be omitted.
- If **cache\_keys** contains duplicates, the last entry takes effect.
- Configure a memory pool before calling this API.

### deallocate\_cache

**Description**

Frees a Cache.

If a Cache is associated with **CacheKey** objects when allocated, its resources are not freed until each associated **CacheKey** has either been pulled or removed by calling **remove\_cache\_key**.

**Prototype**

```python
deallocate_cache(cache: Cache)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache | Cache | Cache to free. |

**Example**

```python
from llm_datadist import *
...
cache_manager.deallocate_cache(cache)
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

No operation if the Cache does not exist or has already been freed.

### remove\_cache\_key

**Description**

Removes a **CacheKey**.

After its **CacheKey** is removed, the Cache can no longer be pulled through **pull\_cache**.

**Prototype**

```python
remove_cache_key(cache_key: CacheKey)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_key | CacheKey | **CacheKey** to remove. |

**Example**

```python
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
cache_manager.remove_cache_key(cache_keys[0])
cache_manager.remove_cache_key(cache_keys[1])
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

No operation if the **CacheKey** does not exist or has already been removed.

### copy\_cache

**Description**

Copies a Cache.

**Prototype**

```python
copy_cache(dst: Cache, src: Cache, dst_batch_index: int = 0, src_batch_index: int = 0, offset: int = 0, size: int = -1, req_id: Optional[int] = None)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| dst | Cache | Destination Cache. |
| src | Cache | Source Cache. |
| dst_batch_index | int | Destination Cache **batch_index**. Default: 0. |
| src_batch_index | int | Source Cache **batch_index**. Default: 0. |
| offset | int | Offset per tensor. Default: 0. |
| size | int | An integer greater than 0 specifies the size to copy.<br>**-1** copies the full data.<br>Default: **-1**. |
| req_id | Optional[int] | **req_id** associated with this call. If set, local diagnostic logs include this **req_id**.<br>Default: **None**. |

**Example**

```python
from llm_datadist import *
...
cache_manager.copy_cache(dst_cache, src_cache, 0, 1, 0, 128)
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

Source and destination Cache **CacheDesc** values must match.

### allocate\_blocks\_cache

**Description**

Allocates a multiblock Cache in PagedAttention scenarios. Free it through **deallocate\_blocks\_cache** after successful allocation.

**Prototype**

```python
allocate_blocks_cache(cache_desc: CacheDesc, blocks_cache_key: Optional[BlocksCacheKey] = None)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| blocks_cache_key | Optional[BlocksCacheKey] | Index identifying a blocks cache. |

**Example**

```python
from llm_datadist import *
...
blocks_cache_key = BlocksCacheKey(1, 0)
blocks_cache = cache_manager.allocate_blocks_cache(cache_desc, blocks_cache_key)
```

**Return Value**

Returns a **Cache** on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

Configure a memory pool before use.

### deallocate\_blocks\_cache

**Description**

Frees a Cache allocated by **allocate\_blocks\_cache** in PagedAttention scenarios.

**Prototype**

```python
deallocate_blocks_cache(cache: Cache)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache | Cache | Cache to free. |

**Example**

```python
from llm_datadist import BlocksCacheKey
...
cache_manager.deallocate_blocks_cache(blocks_cache)
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

None

### copy\_blocks

**Description**

Copies blocks in PagedAttention scenarios.

**Prototype**

```python
copy_blocks(cache: Cache, copy_block_info: Dict[int, List[int]])
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache | Cache | Destination Cache. |
| copy_block_info | Dict[int, List[int]] | Dictionary entries represent (source block index, destination block index list). |

**Example**

```python
cache_manager.copy_blocks(cache, {1: [2,3]})
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

**Constraints**

None

### swap\_blocks

**Description**

Swaps data between **cpu\_cache** and **npu\_cache**.

Swap out uses 4 threads for parallel tasks; swap in uses 1 D2D thread. Binding the process to CPU cores is recommended for stable performance.

Swap in has H2D and D2D stages. For performance, this API allocates a buffer of 4 blocks for pipelined copying. Reserving sufficient Device memory is recommended to avoid OOM.

**Prototype**

```python
swap_blocks(src_cache: Cache, dst_cache: Cache, src_to_dst: Dict[int, int])
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| src_cache | Cache | Source Cache. |
| dst_cache | Cache | Destination Cache. |
| src_to_dst | Dict[int, int] | Dictionary entries represent (source block index, destination block index). |

**Example**

```python
from llm_datadist import Cache
npu_cache = cache_manager.allocate_blocks_cache(npu_cache_desc, npu_cache_key)
cpu_cache = Cache.create_cpu_cache(cpu_cache_desc, cpu_addrs) # cpu_addrs comes from the created CPU tensors.
# swap in
cache_manager.swap_blocks(cpu_cache, npu_cache, {1:2, 3:4})
# swap out
cache_manager.swap_blocks(npu_cache, cpu_cache, {1:2, 3:4})
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type or mismatched **src** and **dst**.

Raises **AttributeError** if the input parameter is **None**.

**Constraints**

Supported only in PagedAttention scenarios.

## LLMConfig

### mem\_pool\_cfg

**Description**

Configures memory pool settings.

**Prototype**

```python
mem_pool_cfg(mem_pool_cfg)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| mem_pool_cfg | string | JSON array string containing **memory_size** and **page_shift**. |

| Setting | Optional/Required | Description |
| --- | --- | --- |
| memory_size | Required | Current memory pool size. **int** greater than 0, in bytes. |
| page_shift | Optional | Shift used to calculate **page_size**. Allocations are aligned to multiples of **page_size**; choose an appropriate size for your scenario.<br>Type: **int**, range [10, 31).<br>Example: **page_shift = 16** gives **page_size = 1<<16=65536**.<br>Default: 16. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.mem_pool_cfg= "{\"memory_size\": 18737418240, \"page_shift\": 16}"
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

### host\_mem\_pool\_cfg

**Description**

Configures Host memory pool settings.

**Prototype**

```python
host_mem_pool_cfg(host_mem_pool_cfg)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| host_mem_pool_cfg | string | JSON array string containing **memory_size** and **page_shift**. |

| Setting | Optional/Required | Description |
| --- | --- | --- |
| memory_size | Required | Current memory pool size. **int** greater than 0, in bytes. |
| page_shift | Optional | Shift used to calculate **page_size**. Allocations are aligned to multiples of **page_size**; choose an appropriate size for your scenario.<br>Type: **int**, range [10, 31).<br>Example: **page_shift = 16** gives **page_size = 1<<16=65536**.<br>Default: 16. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.host_mem_pool_cfg= "{\"memory_size\": 18737418240, \"page_shift\": 16}"
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

The Host memory pool cannot exceed 20G.
