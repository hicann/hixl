# Features

<!-- md-trans-meta sourceCommit=dcf0027ffb4408446bb3ae5b810e44f20f19cc3c translatedAt=2026-11-19T01:01:42.859Z pushedAt=2026-11-19T01:04:51.859Z nl=2 -->

## Connection Management

**Feature Overview**

HCCL collective communication during NN model execution is two-sided: both ends initiate connections simultaneously. PD disaggregation simplifies this to Client-only initiation. Either P or D can act as Client or Server; users can adjust these roles as needed. This example uses D as Client, initiating connections to P.

The main APIs are `link_clusters` and `unlink_clusters`, both called on the Client for point-to-point connections.

- `link_clusters` establishes inter-node connections
- `unlink_clusters` disconnects inter-node connections

**Scenarios**

- PD connections are required before KV Cache transfers can be enabled.
- For cluster reliability, disconnect faulty P or D nodes to take them offline while retaining overall cluster availability.
- Use connection establishment and disconnection to adjust the PD cluster ratio dynamically. Dynamically add or remove the corresponding machine nodes based on workload. Adding nodes requires connection establishment, while removing nodes requires disconnection.

**Example**

The following pseudocode illustrates the process of establishing a connection between 1P and 1D.

1. Start P and D scripts and call the LLM-DataDist initialization API in the scripts. Set the listening Host IP and port on P.

    ```python
    # P-side script
    from llm_datadist import LLMDataDist, LLMRole, LLMStatusCode, LLMClusterInfo, LLMConfig

    # Initialize llm datadist
    llm_datadist = LLMDataDist(LLMRole.PROMPT, cluster_id=0)
    llm_config = LLMConfig()
    llm_config.listen_ip_info = "192.168.1.1:26000" # local_host_ip + port
    llm_config.device_id = 0
    llm_options = llm_config.generate_options()
    llm_datadist.init(llm_options)
    ```

    ```python
    # D-side script
    from llm_datadist import LLMDataDist, LLMRole, LLMStatusCode, LLMClusterInfo, LLMConfig

    # Initialize llm datadist
    llm_datadist = LLMDataDist(LLMRole.DECODER, cluster_id=1)
    llm_config = LLMConfig()
    llm_config.device_id =0
    llm_options = llm_config.generate_options()
    llm_datadist.init(llm_options)
    ```

2. Call `link_clusters` in the D script to connect. On application exit, call `unlink_clusters` on D to disconnect.

    ```python
    # Generate cluster info for connection establishment
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = 0  # remote_cluster_id must match the LLMDataDist instance created on the Prompt side
    cluster.append_local_ip_info("192.168.2.1", 26000) # local_ip_info uses the local Host IP for the connection
    cluster.append_remote_ip_info("192.168.1.1", 26000) # remote_ip_info uses the peer Host IP for the connection

    # Call link_clusters to connect
    # ret is the API return status; rets contains each cluster's connection result
    ret, rets = llm_datadist.link_clusters([cluster], timeout=5000)
    # Check connection results
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("link failed.")
    for cluster_i in range(len(rets)):
        link_ret = rets[cluster_i]
        if link_ret != LLMStatusCode.LLM_SUCCESS:
            print(f"{cluster_i} link failed.")
    ```

3. On exit, D disconnects; both P and D call `llm_datadist.finalize` to release resources.

    ```python
    # P-side script
    # Call llm_datadist to allocate KV Cache
    # Run application inference
    # ...

    # Exit the application
    llm_datadist.finalize()
    ```

    ```python
    # D-side script
    # Call pull_cache and run model inference
    # ...

    # On exit, call unlink_clusters to disconnect
    ret, rets = llm_datadist.unlink_clusters([cluster], timeout=5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise RuntimeError(f'[unlink_cluster] failed, ret={ret}')
    llm_datadist.finalize()
    ```

Repeat the above procedure when adding or restoring a node. When taking a node offline, D normally calls `unlink_clusters`. If D cannot, P must call it.

Call `link_clusters` when bringing nodes online and `unlink_clusters` when taking them offline for flexible distributed cluster scaling.

**Exception Handling**

If a D-side fault prevents `unlink_clusters`, P must call it to clean up resources; otherwise, reconnection fails.

`unlink_clusters` supports forced disconnection for faulty connections, where normal disconnection may take longer. With `force=True`, both ends must call it, and each call cleans up only local connections.

```python
# Force disconnection
ret, rets = llm_datadist.unlink_clusters([cluster], timeout=5000, force=True)
if ret != LLMStatusCode.LLM_SUCCESS:
    raise RuntimeError(f'[unlink_clusters] failed, ret={ret}')
```

## KV Cache Management

**Feature Overview**

The main KV Cache management APIs and their functions are as follows:

**Table 1** Main KV Cache Management APIs and Their Functions

|API|Function|
|--|--|
|register_cache|Registers user-allocated cache memory in non-PA scenarios.|
|register_blocks_cache|Registers user-allocated cache memory in PA scenarios.|
|unregister_cache|Unregisters a registered cache when no longer needed.|
|pull_cache|Pulls remote KV into a local KV Cache based on CacheKey, which must match the CacheKey used in allocate_cache.|
|pull_blocks|Pulls KV in PA scenarios. Unlike pull_cache, pull_blocks retrieves KV Cache from positions specified by block_index.|
|transfer_cache_async|Transfers Cache layers asynchronously.|
|push_blocks|Pushes local KV to a remote KV Cache in PA scenarios.|
|push_cache|Pushes local KV to a remote KV Cache in non-PA scenarios.|

**Scenarios**

Primarily used for KV Cache transfers between distributed clusters.

**Example (Ordinary Cache Transfers)**

This example describes how to use APIs for KV Cache registration, unregistration, and transfer in a general Cache transfer scenario, with pseudocode for each application role.

1. Initialize LLMDataDist on P and D following the example in [Connection Management](functions.md#connection-management).
2. Allocate KV Cache for each model layer on P and D using the calculated size. The upper-layer framework manages and reuses it across requests, then frees it when the application finishes.

    ```python
    import torchair
    import torch
    import torch_npu
    from llm_datadist import CacheDesc, DataType
    # Obtain cache_manager from initialized llm_datadist
    cache_manager = llm_datadist.cache_manager
    # Create CacheDesc from the model's KV Cache shape and tensor count; replace the example shape with the actual model shape
    cache_desc = CacheDesc(num_tensors=4, shape=[4, 4, 8], data_type=DataType.DT_FLOAT16)
    kv_tensors = [
        torch.full(cache_desc.shape, 1, dtype=torch.float16).npu()
        for _ in range(cache_desc.num_tensors)
    ]
    kv_cache = cache_manager.register_cache(cache_desc, [int(tensor.data_ptr()) for tensor in kv_tensors])

    # After connection establishment, pass registered kv_tensors to model inference to generate KV Cache, then send model output to incremental inference as input
    ```

3. Connect LLMDataDist on P and D following the example in [Connection Management](functions.md#connection-management).
4. Transfer KV Cache from P to D using either method:
    - Call `pull_cache` on D to pull the request's KV Cache into allocated memory.

        ```python
        # Create the same cache_key used when allocating the cache on P to pull the corresponding KV cache
        cache_key = CacheKey(prompt_cluster_id=0, req_id=1, model_id=0)
        cache_manager.pull_cache(cache_key, kv_cache, batch_index=1) # Pull into batch index 1
        ```

    - Call `transfer_cache_async` on P to transfer data to Decode.

        ```python
        from llm_datadist import LayerSynchronizer, TransferConfig

        class LayerSynchronizerImpl(LayerSynchronizer):
            def __init__(self, events):
                self._events = events

            def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
                self._events[layer_index].wait()
                return True

        events = [torch.npu.Event() for _ in range(cache_desc.num_tensors // 2)]
        # Run the model; record layer completion with events[layer_index].record()
        # Model execution is implemented by the user
        # user_model.Predict(kv_tensors, events)

        # After model submission, call transfer_cache_async with the tensor memory addresses of each layer of Decode's allocated KV Cache
        transfer_config = TransferConfig(DECODER_CLUSTER_ID, decoder_kv_cache_addrs)
        cache_task = cache_manager.transfer_cache_async(kv_cache, LayerSynchronizerImpl(events), [transfer_config])
        # Wait synchronously for transfer results
        cache_task.synchronize()
        ```

5. Using torch as an example, convert KV Cache to torch tensors for incremental model inference.

    ```python
    # Convert allocated KV Cache to the framework's KV Cache type. Each framework must provide an API to construct its KV Cache from addresses. This example uses PyTorch
    # Conversion and pull can occur in either order
    kv_tensor_addrs = kv_cache.per_device_tensor_addrs[0]
    kv_tensors = torchair.llm_datadist.create_npu_tensors(kv_cache.cache_desc.shape, torch.float16, kv_tensor_addrs)
    # Split converted tensors into the framework's required KV pairs; custom KV grouping is supported
    mid = len(kv_tensors) // 2
    k_tensors = kv_tensors[: mid]
    v_tensors = kv_tensors[mid:]
    kv_cache_tensors = list(zip(k_tensors, v_tensors))

    # Pass converted kv_tensors to the model for iterative inference
    # Wait for the request's incremental inference to complete
    ```

6. Unregister each request's KV Cache memory according to cache usage in your application.

    ```python
    cache_manager.unregister_cache(cache_id)
    ```

7. On application exit, disconnect the P and D sides following the example in [Connection Management](functions.md#connection-management), then call `finalize` to release resources.

**Example (Blocks Cache Transfers)**

This example describes how to use APIs for KV Cache registration, unregistration, and transfer in a Blocks Cache transfer scenario (block-based cache management), with pseudocode for each application role.

1. Initialize LLMDataDist on P and D following the example in [Connection Management](functions.md#connection-management).
2. For each model layer on the P and D sides, allocate tensors such as torch tensors based on the calculated `num_blocks` and register them with LLMDatadist. In the Blocks Cache scenario, different requests reuse the created KV Cache containing `num_blocks` blocks. The upper-layer framework manages this memory itself and frees the allocated memory when the application finishes.

    ```python
    # P/D sides
    # Obtain kv_cache_manager from initialized llm_datadist
    cache_manager = llm_datadist.cache_manager
    # Create CacheDesc from the model's KV Cache shape and tensor count; PA KV cache usually has shape [num_blocks, block_size,...,...]
    num_blocks = 10
    block_mem_size = 128
    cache_desc = CacheDesc(num_tensors=4, shape=[num_blocks, block_mem_size], data_type=DataType.DT_FLOAT16)
    # Create the request's BlocksCacheKey using the cluster_id configured during llm_datadist initialization
    cache_key = BlocksCacheKey(prompt_cluster_id=0, model_id=0)
    ... # Allocate tensors
    # Register KV Cache memory through register_blocks_cache
    kv_cache = cache_manager.register_blocks_cache(cache_desc, [addr, addr2, ...(tensor addresses)], cache_key)
    ```

3. Connect LLMDataDist on P and D following the example in [Connection Management](functions.md#connection-management).
4. When a new request arrives on the P side, the inference framework assigns the corresponding `block_index` values to the request. After model inference, the request's KV Cache resides in memory at the corresponding `block_index` values. The model output and the request's `block_table` are sent to the D-side inference model as input.
5. For each new request on D, the inference framework assigns `block_index` values. The inference framework then calls `pull_blocks` to transfer KV Cache to the specified location using the mapping between P-side and D-side `block_index` values. Two methods follow:
    - Call `pull_blocks` on D to pull KV Cache.

        ```python
        # Add the request on D using information from P and allocate its block_table
        # Pull KV into the target blocks using the received src_block_table and newly allocated dst_block_table
        cache_key = BlocksCacheKey(prompt_cluster_id=0, model_id=0) # Parameters used in P-side register_blocks_cache
        cache_manager.pull_blocks(cache_key, cache, [0, 1], [2, 3]) # Pull blocks 0, 1 on P into blocks 2, 3 on D
        ```

    - Call `transfer_cache_async` on P to transfer data to D.

        ```python
        # Implement LayerSynchronizerImpl to query layer completion through torch Events; this example uses Events
        class LayerSynchronizerImpl(LayerSynchronizer):
            def __init__(self, events):
                self._events = events

            def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
                self._events[layer_index].wait()
                return True

        events = [torch.npu.Event() for _ in range(cache_desc.num_tensors // 2)]
        # Run the model; record layer completion with events[layer_index].record()
        # This function is implemented by the user
        user_model.Predict(kv_cache_tensors, events)

        # After model submission, call transfer_cache_async with the tensor memory addresses of each layer of Decode's allocated KV Cache
        transfer_config = TransferConfig(DECODER_CLUSTER_ID, decoder_kv_cache_addrs)
        cache_task = cache_manager.transfer_cache_async(kv_cache, LayerSynchronizerImpl(events), [transfer_config], [0, 1], [2, 3])
        # Wait synchronously for transfer results
        cache_task.synchronize()
        ```

6. After the application finishes, call `unregister_cache` on P and D to unregister KV Cache memory.

    ```python
    # Wait for D to finish pulling the request's KV Cache
    # Unregister each request's KV Cache according to cache usage in your application
    cache_manager.unregister_cache(cache_id)
    ```

7. On application exit, disconnect the P and D sides following the cluster disconnection example, then call `finalize` to release resources.

**Exception Handling**

- `LLM_KV_CACHE_NOT_EXIST` means the peer KV Cache does not exist. Check whether the peer process is abnormal or the corresponding request has completed inference. Other requests are unaffected; retry after checking the process.
- `LLM_WAIT_PROCESS_TIMEOUT` means a KV pull timed out, indicating a connection problem. Disconnect, reconnect, and retry.
