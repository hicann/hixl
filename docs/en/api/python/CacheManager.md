# CacheManager

<!-- md-trans-meta sourceCommit=d9da44d0ffc32a2e0ed6900cc2f98cb9432bb453 translatedAt=2026-10-21T07:15:41.588Z pushedAt=2026-10-27T02:46:32.224Z nl=2 -->

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

Note:
<!-- npu="910b" id4 -->
For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id4 -->
<!-- npu="950" id5 -->
Ascend 950PR&950DT products do not support **remap_registered_memory**.
<!-- end id5 -->

## CacheManager Constructor

**Description**

Obtain a **CacheManager** instance through **cache\_manager**.

The **CacheManager** lifetime matches the initialization lifecycle of its **LLMDataDist**. After **LLMDataDist.finalize()**, previously obtained instances are invalid. Cache allocation, registration, release, or transfer through an old instance raises **LLMException** with **LLM_ENGINE_FINALIZED**.

After calling **LLMDataDist.init()** again, obtain a new **CacheManager** through **cache\_manager**. Reinitializing the engine does not restore old instances.

## pull\_cache

**Description**

Pulls data from the corresponding peer node into a local **Cache** based on **CacheKey**.

**Prototype**

```python
pull_cache(cache_key: Union[CacheKey, CacheKeyByIdAndIndex],
           cache: Cache,
           batch_index: int = 0,
           size: int = -1, **kwargs)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_key | Union[CacheKey,CacheKeyByIdAndIndex] | Key identifying the Cache to pull.<br>Use **CacheKey** for **req_id**, **prefix_id**, and **model_id** lookup.<br>Use **CacheKeyByIdAndIndex** for **cache_id** and **batch_index** lookup. |
| cache | Cache | Destination Cache. |
| batch_index | int | Batch index. Default: 0. |
| size | int | An integer greater than 0 specifies the tensor size to pull.<br>**-1** copies the full size of one local KV.<br>Default: **-1**. |
| \*\*kwargs | NA | Standard Python extensible arguments, usually passed as **key=value**.<br>For optional parameters, see Table 1. |

**Table 1** Optional \*\*kwargs Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| src_layer_range | Optional[range] | Optional source layer range for layer-by-layer KV pulls. Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| dst_layer_range | Optional[range] | Optional destination layer range for layer-by-layer KV pulls. Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| tensor_num_per_layer | Optional[int] | Optional tensor count per layer, default 2, ranging from 1 to the cache's total tensor count.<br>If **src_layer_range** or **dst_layer_range** is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

Raises **LLMException** for an invalid **layer\_range**.

**Constraints**

- With **enable\_remote\_cache\_accessible** enabled, **cache\_key** must be **CacheKeyByIdAndIndex**.
- For D2H and H2D transfers, configure a Device memory pool of at least 100M during Host-side initialization.

## pull\_blocks

**Description**

In PagedAttention scenarios, pulls a Cache from a peer node into a local Cache using **BlocksCacheKey** and block lists.

**Prototype**

```python
pull_blocks(src_cache_key: Union[CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey],
            dst_cache: Cache,
            src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
            dst_blocks: Union[Tuple[int], List[int]] = (), **kwargs)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| src_cache_key | Union[CacheKey,CacheKeyByIdAndIndex, BlocksCacheKey] | Remote Cache index. |
| dst_cache | Cache | Destination Cache. |
| src_blocks | Optional[Union[Tuple[int], List[int]]] | Remote block index list. Omit if **src_cache_key** is not **BlocksCacheKey**. |
| dst_blocks | Union[Tuple[int], List[int]] | Local block index list. |
| \*\*kwargs | NA | Standard Python extensible arguments, usually passed as **key=value**.<br>For optional parameters, see Table 1. |

**Table 1** Optional \*\*kwargs Parameters

| Parameter | Data Type | Values |
| --- | --- | --- |
| src_layer_range | Optional[range] | Optional source layer range for layer-by-layer KV pulls. Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| dst_layer_range | Optional[range] | Optional destination layer range for layer-by-layer KV pulls. Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| tensor_num_per_layer | Optional[int] | Optional tensor count per layer, default 2, ranging from 1 to the cache's total tensor count.<br>If **src_layer_range** or **dst_layer_range** is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

- Returns nothing on success.
- Raises **TypeError** or **ValueError** for an incorrect input data type.
- Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.
- Raises **LLMException** for an invalid **layer\_range**.

**Constraints**

- When **src\_cache** is on **HOST** and **dst\_cache** is on **DEVICE**, both must be blocks caches.
- Enabling **enable\_remote\_cache\_accessible** adds additional constraints. See its Constraints section.
- For D2H and H2D transfers, configure a Device memory pool of at least 100M during Host-side initialization.

## register\_cache

**Description**

Registers user-allocated memory in non-PagedAttention scenarios.

**Prototype**

```python
register_cache(cache_desc: CacheDesc, addrs: List[int], cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = (), remote_accessible: Optional[bool] = None) -> Cache:
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| addrs | List[int] | Cache addresses.<br>The total address count across **register_cache** and **register_blocks_cache** cannot exceed 240. |
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache index. |
| remote_accessible | Optional[bool] | Whether registered memory can be used for network transfers. Default: **True** for Device memory and **False** for Host memory. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

Returns the registered cache on success.

Raises **TypeError** or **ValueError** for an incorrect input data type or mismatched **src** and **dst**.

Raises **AttributeError** if the input parameter is **None**.

**Constraints**

- Register Device memory before connection establishment. Host memory has no ordering constraint.

- Ensure that registered memory addresses are not duplicated.

  <!-- npu="A3,910b" id6 -->
- Up to 20GB of Host memory can be registered for HDK earlier than 25.5.0, and 1TB for HDK 25.5.0 or later. Registering more memory consumes more OS memory. This constraint applies to the following models:
  <!-- npu="910b" id7 -->
  - Atlas A2 products
  <!-- end id7 -->
  <!-- npu="A3" id8 -->
  - Atlas A3 products
  <!-- end id8 -->

- For D2D HCCS transfers, align the starting address to 2MB; otherwise, link establishment may fail. HCCS does not support Host memory. This constraint applies to the following chips:
  <!-- npu="910b" id9 -->
  - Atlas A2 products
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3 products
  <!-- end id10 -->

- For RDMA transfers, allocate Host memory through **aclrtMallocHost**; otherwise, link establishment may fail. This constraint applies to the following chips:
  <!-- npu="910b" id11 -->
  - Atlas A2 products
  <!-- end id11 -->
  <!-- npu="A3" id12 -->
  - Atlas A3 products
  <!-- end id12 -->

  <!-- end id6 -->

## register\_blocks\_cache

**Description**

Registers user-allocated memory in PagedAttention scenarios.

**Prototype**

```python
register_blocks_cache(cache_desc: CacheDesc, addrs: List[int], blocks_cache_key: Optional[BlocksCacheKey] = None, remote_accessible: Optional[bool] = None) -> Cache:
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache description. |
| addrs | List[int] | Cache addresses.<br>The total address count across **register_cache** and **register_blocks_cache** cannot exceed 240. |
| blocks_cache_key | Optional[BlocksCacheKey] | Optional **BlocksCacheKey** index. |
| remote_accessible | Optional[bool] | Whether registered memory can be used for network transfers. Default: **True** for Device memory and **False** for Host memory. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

Returns the registered cache on success.

Raises **TypeError** or **ValueError** for an incorrect input data type or a mismatch.

Raises **AttributeError** if the input parameter is **None**.

**Constraints**

<!-- npu="A3,910b" id13 -->
For HCCS transfers with memory not allocated by **aclrtMalloc**, align addresses to the page size (2MB alignment is recommended when the page size is known); otherwise, link establishment may fail. This constraint applies to the following chips:

<!-- npu="910b" id14 -->
- Atlas A2 products
<!-- end id14 -->
<!-- npu="A3" id15 -->
- Atlas A3 products
<!-- end id15 -->
<!-- end id13 -->

<!-- npu="950" id16 -->
No constraints apply to Ascend 950PR&950DT products.
<!-- end id16 -->

## transfer\_cache\_async

**Description**

Transfers Cache layers asynchronously.

**Prototype**

```python
transfer_cache_async(self,
                     src_cache: Cache,
                     layer_synchronizer: LayerSynchronizer,
                     transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]],
                     src_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_memory_size: Optional[int] = None) -> CacheTask
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| src_cache | Cache | Source Cache. |
| layer_synchronizer | LayerSynchronizer | Object implementing **LayerSynchronizer**. |
| transfer_configs | Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]] | List or tuple of transfer configurations. |
| src_block_indices | Optional[Union[List[int], Tuple[int]]] | Source Cache block indices. Set for a PA source Cache. |
| dst_block_indices | Optional[Union[List[int], Tuple[int]]] | Destination Cache block indices. Set for a PA destination Cache. |
| dst_block_memory_size | Optional[int] | Memory size of each destination Cache block. Set for a PA destination Cache.<br>If the source Cache is also PA, this parameter can be omitted and defaults to the source block size.<br>**0** is equivalent to omitting it. |

**Example**

```python
cache_task = cache_manager.transfer_cache_async(cache, LayerSynchronizerImpl(), transfer_configs)
```

**Return Value**

Returns a **CacheTask** on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** for invalid input data.

**Constraints**

- Transfers from **HOST** **src\_cache** to **DEVICE** **dst\_cache** are unsupported.
- Transfers from a PA **src\_cache** to a non-PA **dst\_cache** are unsupported.
- A single process with multiple devices is unsupported.
- A **HOST** **dst\_cache** must be allocated through **allocate\_cache**.
- Ensure that **dst\_addrs** in **transfer\_config** are valid, as is data used in **dst\_block\_indices** transfers. Otherwise, errors are undefined.
- With **enable\_remote\_cache\_accessible** enabled, **transfer\_configs** must contain **TransferWithCacheKeyConfig** objects; otherwise, they must contain **TransferConfig** objects.
- For an ordinary Cache, **src_batch_index** must be within its batch range (**0 ≤ index < batch_size**). Invalid indices raise **LLMException** before the background thread starts. For a PA source Cache, **src_batch_index** must still be 0.

## push\_blocks

**Description**

In PagedAttention scenarios, pushes a local Cache to a remote Cache using **BlocksCacheKey** and block lists.

**Prototype**

```python
push_blocks(self,
                    dst_cache_key: BlocksCacheKey,
                    src_cache: Cache,
                    src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    src_layer_range: range = None,
                    dst_layer_range: range = None,
                    tensor_num_per_layer = _NUM_TENSORS_PER_LAYER)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| dst_cache_key | BlocksCacheKey | Remote Cache index. |
| src_cache | Cache | Local Cache. |
| src_blocks | Optional[Union[Tuple[int], List[int]]] | Local block index list. |
| dst_blocks | Union[Tuple[int], List[int]] | Remote block index list. |
| src_layer_range | Optional[range] | Optional source layer range for layer-by-layer KV pulls. Default: **None**.<br>Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| dst_layer_range | Optional[range] | Optional destination layer range for layer-by-layer KV pulls. Default: **None**.<br>Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| tensor_num_per_layer | Optional[int] | Optional tensor count per layer, default 2, ranging from 1 to the cache's total tensor count.<br>If **src_layer_range** or **dst_layer_range** is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

- Returns nothing on success.
- Raises **TypeError** or **ValueError** for an incorrect input data type.
- Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.
- Raises **LLMException** for an invalid **layer\_range**.

**Constraints**

- When **src\_cache** is on **HOST** and **dst\_cache** is on **DEVICE**, both must be PA caches.
- Enabling **enable\_remote\_cache\_accessible** adds additional constraints. See its Constraints section.
- For D2H and H2D transfers, configure a Device memory pool during Host-side initialization.
- Currently, **src\_layer\_range** and **dst\_layer\_range** must have identical values.

## push\_cache

**Description**

Pushes a local Cache to a remote Cache based on **CacheKey**.

**Prototype**

```python
push_cache(self,
                   dst_cache_key: CacheKeyByIdAndIndex,
                   src_cache: Cache,
                   src_batch_index: int = 0,
                   src_layer_range: range = None,
                   dst_layer_range: range = None,
                   size: int = -1,
                   tensor_num_per_layer = _NUM_TENSORS_PER_LAYER) -> None
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| dst_cache_key | CacheKeyByIdAndIndex | Remote Cache index. |
| src_cache | Cache | Local cache. |
| src_batch_index | int | Local batch index. Default: 0. |
| src_layer_range | Optional[range] | Optional source layer range for layer-by-layer KV pulls. Default: **None**.<br>Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| dst_layer_range | Optional[range] | Optional destination layer range for layer-by-layer KV pulls. Default: **None**.<br>Only **step=1** is supported; omitting it transfers all layers.<br>These are layer indices, not tensor indices. Each layer corresponds to N consecutive tensors (K/V), requiring a KV,...,KV memory layout. Other layouts are unsupported. N is **tensor_num_per_layer**, default 2. |
| tensor_num_per_layer | Optional[int] | Optional tensor count per layer, default 2, ranging from 1 to the cache's total tensor count.<br>If **src_layer_range** or **dst_layer_range** is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault value must divide the cache's total tensor count evenly. |
| size | int | Transfer size per tensor. Only the default **-1**, the local single-KV size, is currently supported. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

Raises **LLMException** if execution exceeds **sync\_kv\_timeout**.

Raises **LLMException** for an invalid **layer\_range**.

**Constraints**

- When **src\_cache** is on **HOST** and **dst\_cache** is on **DEVICE**, both must be contiguous caches.
- With **enable\_remote\_cache\_accessible** enabled, **cache\_key** must be **CacheKeyByIdAndIndex**.
- For D2H and H2D transfers, configure a Device memory pool during Host-side initialization.
- Currently, **src\_layer\_range** and **dst\_layer\_range** must have identical values.

## remap\_registered\_memory

**Description**

If a memory UCE occurs during large-model inference, returning **ACL\_ERROR\_RT\_DEVICE\_MEM\_ERROR**, the upper-layer framework must first determine whether the affected memory is KV Cache memory. If not, refer to the documentation for **torch\_npu.npu.restart\_device** in [PyTorch](https://www.hiascend.com/developer/software/ai-frameworks/pytorch) to obtain and repair the faulty virtual addresses. For KV Cache memory, also call this API to repair KV Cache memory registered with the NIC.

> [!NOTE]
>Reserved API; currently unsupported.

**Prototype**

```python
remap_registered_memory(mem_infos: Union[MemInfo, list[MemInfo]]) -> None
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| mem_infos | Union[MemInfo, list[MemInfo]] | Memory information or a list of memory information entries. |

**Example**

```python
from llm_datadist import *
role = LLMRole.PROMPT  # Role affected by the fault.
cluster_id = 1  # LLMDataDist ID affected by the fault.
datadist = LLMDataDist(role, cluster_id)
cache_manager = datadist.cache_manager
addr = 12345678 # Faulty address.
size = 1024  # Size of the faulty address region.
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, addr, size)
cache_manager.remap_registered_memory(mem_info)
```

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.
<!-- npu="950" id17 -->
<br>Ascend 950PR&950DT products do not support this API.
<!-- end id17 -->

**Constraints**

Currently, only Device memory repair is supported.

## unregister\_cache

**Description**

Unregisters user-allocated memory.

**Prototype**

```python
unregister_cache(cache_id: int) -> None
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| cache_id | int | Cache ID returned by **register_cache** or **register_blocks_cache**. |

**Example**

See [Running the Examples](../../../../examples/python/README_en.md).

**Return Value**

Returns nothing on success.

Raises **TypeError** or **ValueError** for an incorrect input data type.

**Constraints**

- Currently supported only when **local\_comm\_res** is configured.
- Call **unlink** to disconnect before this API; otherwise, HCCL reports an error.
