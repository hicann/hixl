# 功能介绍

## 链路管理

**功能介绍**

NN模型执行时调用的HCCL集合通信接口是双边通信，即需要两边同时发起建链，而在P-D分离方案中，简化建链操作，由Client单侧发起建链。不限制P/D任意一侧作为Client或者Server，用户可以根据需求自行调整，我们示例将D定义为Client端，建链过程实现由D向P发起建链的流程。

主要提供的是link\_clusters和unlink\_clusters两个接口，都是由Client侧进行调用，建链行为是点对点的。

- link\_clusters用于节点之间的建链
- unlink\_clusters用于节点之间的断链

**使用场景**

- 建链操作是PD之间进行KV Cache传输的前提，所以想使能KV Cache传输功能，需要先进行建链。
- 集群可靠性场景下，当P或者D集群节点出现异常时，在不影响整个集群可用性前提下，通过断链下线对应故障节点。
- 通过建链和断链动态调整PD集群配比。根据闲忙，动态的增加或减少对应的机器节点。增加节点需要建链，减少节点需要断链。

**功能示例**

此处代码示例为1P1D之间建链的伪代码流程。

1. 拉起P和D侧脚本，脚本中调用LLM-DataDist的初始化接口。P侧需要设置侦听的Host IP和port。

    ```python
    # P侧脚本
    from llm_datadist import LLMDataDist, LLMRole, LLMStatusCode, LLMClusterInfo
    
     # llm datadist初始化
     llm_datadist = LLMDataDist(LLMRole.Prompt, cluster_id=0)
     llm_config = LLMConfig()
     llm_config.listen_ip_info = "10.10.1.1:26000" # local_host_ip + port
     llm_config.device_id = 0
     llm_options = llm_config.generate_options()
     llm_datadist.init(llm_options)
    ```

    ```python
    # D侧脚本
    from llm_datadist import LLMDataDist, LLMRole, LLMStatusCode, LLMClusterInfo
    
     # llm datadist初始化
     llm_datadist = LLMDataDist(LLMRole.DECODER, cluster_id=0)
     llm_config = LLMConfig()
     llm_config.device_id =0 
     llm_options = llm_config.generate_options()
     llm_datadist.init(llm_options)
    ```

2. 在D侧脚本中调用link\_clusters发起建链操作，当业务退出时在D侧调用unlink\_clusters进行断链。

    ```python
     # 生成cluster info信息用于建链
     cluster = LLMClusterInfo()
     cluster.remote_cluster_id = 1  # 此处的remote_cluster_id需要和P侧创建的LLMDataDist对应
     cluster.append_local_ip_info("192.168.2.1", 26000) # local_ip_info的IP是本机需要建链的Host IP地址
     cluster.append_remote_ip_info("192.168.1.1", 26000) # remote_ip_info的IP是想和对端建链的Host IP地址
    
     # 调用link_clusters进行建链
     # ret是接口的返回值，rets表示每个cluster建链的结果。
     ret, rets = llm_datadist.link_clusters([cluster], timeout=5000)
     # 判断建链结果
     if ret != LLMStatusCode.LLM_SUCCESS:
         raise Exception("link failed.")
     for cluster_i in range(len(rets)):
         link_ret = rets[cluster_i]
         if link_ret != LLMStatusCode.LLM_SUCCESS:
             print(f"{cluster_i} link failed.")
    ```

3. 业务结束D侧进行断链，P和D都调用llm\_datadist的finalize释放资源。

    ```python
     # P侧脚本
     # 调用llm_datadist申请KV Cache
     # 执行业务推理
     # ...
    
     # 业务退出
     llm_datadist.finalize()
    ```

    ```python
     # D侧脚本
     # pull_cache、模型推理
     # ...
    
     # 业务退出，调用unlink_clusters进行断链
     ret, rets = llm_datadist.unlink_clusters([cluster], timeout=5000)
     if ret != LLMStatusCode.LLM_SUCCESS:
         raiseRuntimeError(f'[unlink_cluster] failed, ret={ret}')
     llm_datadist.finalize()
    ```

当新增节点或者已下线节点再上线时，需要执行一遍上述使用流程。当下线节点时，正常情况下D侧需要主动调用unlink\_clusters接口_，_如果D侧无法调用unlink\_clusters接口，则需要P侧调用unlink\_clusters。

通过节点上线调用link\_clusters，节点下线调用unlink\_clusters来灵活地进行分布式集群的动态扩缩容。

**异常处理**

当D侧出现异常导致无法调用unlink\_clusters时，需要由P侧调用unlink\_clusters进行资源清理，否则无法再次进行建链。

unlink\_clusters接口提供了强制断链的能力，该能力适用于链路故障时，普通断链操作会耗时比较久。使用强制断链接口（设置force=True），需要两侧都发起调用，只会清理本端的链接。

```python
# 强制断链
 ret, rets = llm_datadist.unlink_clusters([cluster], timeout=5000, force=True)
 if ret != LLMStatusCode.LLM_SUCCESS:
     raise RuntimeError(f'[unlink_clusters] failed, ret={ret}')
```

## KV Cache管理

**功能介绍**

KV Cache管理涉及的主要接口及功能如下：

**表1**  KV Cache管理的主要接口及功能

|接口名称|功能|
|--|--|
|register_cache|注册cache。非PA场景下，调用此接口注册一个自行申请的内存。|
|register_blocks_cache|注册cache。PA场景下，调用此接口注册一个自行申请的内存。|
|unregister_cache|当cache不再使用时，调用当前接口对注册过的cache进行解注册。|
|pull_cache|根据CacheKey，从远端节点拉取KV到本地KV Cache。该CacheKey需要和allocate_cache的CacheKey保持一致。|
|pull_blocks|PA场景下，KV的拉取接口。和pull_cache的差异是，pull_blocks是按照block_index拉取的对应位置的KV Cache。|
|transfer_cache_async|异步分层传输Cache。|
|push_blocks|PA场景下，KV的推送接口，从本地节点推送KV到远端KV Cache。|
|push_cache|非PA场景下，从本地节点推送KV到远端KV Cache。|

**使用场景**

主要用于分布式集群间的KV Cache传输。

**功能示例（一般Cache传输场景）**

本示例介绍一般Cache传输场景下接口的使用，主要涉及KV Cache的注册、解注册、传输。如下将根据业务角色给出伪代码示例。

1. P侧和D侧根据[链路管理](functions.md#链路管理)章节的示例完成LLMDataDist的初始化。
2. 在P侧和D侧模型的每层按照计算好的大小提前申请KV Cache。不同请求对创建的KV Cache进行复用，上层框架自行管理，业务结束后释放申请的内存。

    ```python
    import torchair
    import torch
    import torch_npu
    # 从已初始化的llm_datadist中获取cache_manager
    cache_manager = llm_datadist.cache_manager
    # 根据模型中KV Cache的shape以及总个数创建CacheDesc。此处shape只是示例，实际填写网络中的KV cache shape。
    cache_desc = CacheDesc(num_tensors=4, shape=[4, 4, 8], data_type=DataType.DT_FLOAT16)
    tensor1 = torch.full((4, 4, 8), 1, dtype=torch.float).npu()
    ... # 其他tensor申请
    cache = cache_manager.register_cache(cache_desc, [int(tensor.data_ptr()), int(tensor2.data_ptr()) ...])
    
    # 建链后将注册的kv_tensors传给模型推理计算产生KV Cache，将模型输出传输给增量推理模型作为输入
    ```

3. P侧和D侧根据[链路管理](functions.md#链路管理)章节的示例完成LLMDataDist间的建链。
4. 将KV Cache从P侧传输到D侧，有以下两种方式：
    - 在D侧调用pull\_cache接口拉取对应请求的KV Cache到申请的内存中。

        ```python
        # 创建和P侧申请cache时相同的cache_key，用于拉取对应的KV cache
        cache_key = CacheKey(prompt_cluster_id=0, req_id=1, model_id=0)
        cache_manager.pull_cache(cache_key, kv_cache, batch_index=1) # 拉到batch index为1的位置上
        ```

    - 在P侧调用transfer\_cache\_async接口将数据传输至Decode。

        ```python
        from llm_datadist import LayerSynchronizer, TransferConfig
        
        class LayerSynchronizerImpl(LayerSynchronizer):
            def __init__(self, events):
                self._events = events
        
            def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
                self._events[layer_index].wait()
                return True
        
        events = [torch.npu.Event() for _ in range(cache_desc.num_tensors // 2)]
        # 执行模型，模型在各层计算完成后调用events[layer_index].record()记录完成状态
        # 模型执行由用户实现
        # user_model.Predict(kv_tensors, events)
        
        # 模型下发完成后，调用transfer_cache_async传输数据，此处需要填写Decode已申请的KV Cache各层tensor的内存地址
        transfer_config = TransferConfig(DECODER_CLUSTER_ID, decoder_kv_cache_addrs)
        cache_task = cache_manager.transfer_cache_async(kv_cache, LayerSynchronizerImpl(events), [transfer_config])
        # 同步等待传输结果
        cache_task.synchronize()
        ```

5. 以torch为例，将KV Cache转换为torch tensor，进行增量模型推理。

    ```python
    # 将申请好的KV Cache转换为框架中的KV Cache类型，不同框架中都需要提供根据KV Cache地址创建对应类型的KV Cache的接口。此处以PyTorch为例
    # 转换操作和pull操作顺序不分先后
    kv_tensor_addrs = kv_cache.per_device_tensor_addrs[0]
    kv_tensors = torchair.llm_datadist.create_npu_tensors(kv_cache.cache_desc.shape, torch.float16, kv_tensor_addrs)
    # 将转换后的tensor拆分为框架需要的KV配对方式，可以自定义组合KV
    mid = len(kv_tensors) // 2
    k_tensors = kv_tensors[: mid]
    v_tensors = kv_tensors[mid:]
    kv_cache_tensors = list(zip(k_tensors, v_tensors))
    
    # 将转换的kv_tensors传给模型进行迭代推理
    # 等待请求增量推理完成
    ```

6. 根据业务中cache的使用时机自行解注册对应请求的KV Cache内存。

    ```python
    cache_manager.unregister_cache(cache_id)
    ```

7. 业务退出时，P侧和D侧根据[链路管理](functions.md#链路管理)章节的示例进行断链，然后调用finalize接口释放资源。

**功能示例（Blocks Cache传输场景）**

本示例介绍Blocks Cache（将Cache使用块状形式管理）传输场景下接口的使用，主要涉及KV Cache的注册、解注册、传输。如下将根据业务角色给出伪代码示例。

1. P侧和D侧根据[链路管理](functions.md#链路管理)的示例完成LLMDataDist的初始化。
2. 在P侧和D侧模型的每层按照计算好的num\_blocks数量调用申请tensor，比如torch tensor，并注册给LLMDatadist。Blocks Cache场景下，不同请求对创建的num\_blocks大小的KV Cache进行复用，上层框架自行管理，业务结束后释放申请的内存。

    ```python
    # P/D侧
    # 从已经初始化的llm_datadist中获取kv_cache_manager
    cache_manager = llm_datadist.cache_manager
    # 根据模型中KV Cache的shape以及总个数创建CacheDesc。PA场景的KV cache shape通常为[num_blocks, block_size,...,...]
    num_blocks = 10
    block_mem_size = 128
    cache_desc = CacheDesc(num_tensors=4, shape=[num_blocks, block_mem_size], data_type=DataType.DT_FLOAT16)
    # 根据初始化llm_datadist时的cluster_id创建对应请求的BlocksCacheKey
    cache_key = BlocksCacheKey(prompt_cluster_id=0, model_id=0)
    ... # 申请tensor
    # 调用register_blocks_cache接口注册KV Cache内存
    kv_cache = cache_manager.register_blocks_cache(cache_desc, [addr, addr2, ...(tensor地址)], cache_key)
    ```

3. P侧和D侧根据[链路管理](functions.md#链路管理)章节的示例完成LLMDataDist间的建链。
4. P侧有新请求进来后，推理框架会给每个请求分配好对应的block\_index。模型推理完之后，该请求对应的KV Cache就在对应的block\_index所在的内存上，将模型输出和请求对应的block\_table传输给D侧推理模型作为输入。
5. D侧有新请求进来后，推理框架也会给每个请求分配好对应的block\_index，然后调用pull\_blocks接口，根据P侧的block\_index和D侧的block\_index的对应关系，将KV Cache传输到指定位置。此时有两种方式：
    - 在D侧调用pull\_blocks接口拉取KV Cache。

        ```python
        # D侧根据P侧传过来的信息，添加新请求，并申请对应的block_table
        # D侧根据传过来请求的src_block_table和新申请的dst_block_table拉取KV到对应block
        cache_key = BlocksCacheKey(prompt_cluster_id=0, model_id=0) # P侧register_blocks_cache时的入参
        cache_manager.pull_blocks(cache_key, cache, [0, 1], [2, 3]) # 将P侧0, 1 block位置上的数据拉到D侧2, 3 block位置上
        ```

    - 在P侧调用transfer\_cache\_async接口时将数据传输至D侧。

        ```python
        # 实现LayerSynchronizerImpl，通过torch Event获取各层计算结束状态，本例中通过Event机制实现
        class LayerSynchronizerImpl(LayerSynchronizer):
            def __init__(self, events):
                self._events = events
        
            def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
                self._events[layer_index].wait()
                return True
        
        events = [torch.npu.Event() for _ in range(cache_desc.num_tensors // 2)]
        # 执行模型,模型在各层计算完成后调用events[layer_index].record()记录完成状态
        # 该函数由用户实现
        user_model.Predict(kv_cache_tensors, events)
        
        # 模型下发完成后，调用transfer_cache_async传输数据，此处需要填写Decode已申请的KV Cache各层tensor的内存地址
        transfer_config = TransferConfig(DECODER_CLUSTER_ID, decoder_kv_cache_addrs)
        cache_task = cache_manager.transfer_cache_async(kv_cache, LayerSynchronizerImpl(events), [transfer_config], [0, 1], [2, 3])
        # 同步等待传输结果
        cache_task.synchronize()
        ```

6. 业务结束后P侧和D侧调用unregister\_cache对注册的KV Cache内存进行解注册。

    ```python
    # 等待D侧拉取完对应请求的KV Cache
    # 根据业务中cache的使用时机自行解注册对应请求的KV Cache。
    cache_manager.unregister_cache(cache_id)
    ```

7. 业务退出时，P侧和D侧根据集群断链的示例进行断链，然后调用finalize接口释放资源。

**异常处理**

- 错误码LLM\_KV\_CACHE\_NOT\_EXIST表示对端KV Cache不存在，需要检查对端进程是否异常或者对应KV Cache的请求有没有推理完成。该错误不影响其他请求流程，确认流程后可以重试。
- 错误码LLM\_WAIT\_PROCESS\_TIMEOUT表示pull KV超时，说明链路出现问题，需要重新断链建链再尝试。
