# CacheManager

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。针对Ascend 950PR/Ascend 950DT，不支持remap_registered_memory。

## CacheManager构造函数

**函数功能**

CacheManager的实例应该通过cache\_manager返回。

## pull\_cache

**函数功能**

根据CacheKey，从对应的对端节点拉取到本地Cache。

**函数原型**

```
pull_cache(cache_key: Union[CacheKey, CacheKeyByIdAndIndex],
           cache: Cache,
           batch_index: int = 0,
           size: int = -1, **kwargs)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_key | Union[CacheKey,CacheKeyByIdAndIndex] | 需要被拉取的CacheKey。<br>通过req_id，prefix_id，model_id拉取则传入CacheKey。<br>通过cache_id，batch_index拉取则传入CacheKeyByIdAndIndex。 |
| cache | Cache | 目标Cache。 |
| batch_index | int | batch index，默认为0。 |
| size | int | 设置为>0的整数，表示要拉取的tensor大小。<br>或设置为-1，表示完整拷贝：本地单个KV的大小。<br>默认为-1。 |
| **kwargs | NA | 这个是Python函数的可扩展参数通用写法，一般通过key=value的方式直接传入参数。<br>可选参数的详细信息请参考表1。 |

**表 1**  \*\*kwargs的可选参数

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| src_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| dst_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| tensor_num_per_layer | Optional[int] | 可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。

layer\_range参数异常会抛出LLMException异常。

**约束说明**

- 开启enable\_remote\_cache\_accessible时，只支持cache\_key类型为CacheKeyByIdAndIndex。
- 在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池, 且内存池大小至少配置为100M。

## pull\_blocks

**函数功能**

PagedAttention场景下，根据BlocksCacheKey，通过block列表的方式从对端节点拉取Cache到本地Cache。

**函数原型**

```
pull_blocks(src_cache_key: Union[CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey],
            dst_cache: Cache,
            src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
            dst_blocks: Union[Tuple[int], List[int]] = (), **kwargs)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| src_cache_key | Union[CacheKey,CacheKeyByIdAndIndex, BlocksCacheKey] | 远端的Cache索引。 |
| dst_cache | Cache | 目标Cache。 |
| src_blocks | Optional[Union[Tuple[int], List[int]]] | 远端的block index列表，src_cache_key不是BlocksCacheKey时，不填。 |
| dst_blocks | Union[Tuple[int], List[int]] | 本地的block index列表。 |
| **kwargs | NA | 这个是Python函数的可扩展参数通用写法，一般通过key=value的方式直接传入参数。<br>可选参数的详细信息请参考表1。 |

**表 1**  \*\*kwargs的可选参数

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| src_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| dst_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| tensor_num_per_layer | Optional[int] | 可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

- 正常情况下无返回值。
- 传入数据类型错误情况下会抛出TypeError或ValueError异常。
- 执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。
- layer\_range参数异常会抛出LLMException异常。

**约束说明**

- 当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为blocks cache的场景。
- 开启enable\_remote\_cache\_accessible时会引入额外约束，详见该配置的约束说明章节。
- 在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池，且内存池大小至少配置为100M。

## register\_cache

**函数功能**

非PagedAttention场景下，调用此接口注册一个自行申请的内存。

**函数原型**

```
register_cache(cache_desc: CacheDesc, addrs: List[int], cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = (), remote_accessible: Optional[bool] = None) -> Cache:
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache的描述信息。 |
| addrs | List[int] | Cache的地址。register_cache中的地址个数与register_blocks_cache的地址个数之和不超过240。 |
| cache_keys | Union[Tuple[CacheKey], List[CacheKey]] | Cache的索引。 |
| remote_accessible | Optional[bool] | 指定当前注册内存是否能用来做网络传输，对于Device内存，默认值是True，对于Host内存，默认值是False。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

正常情况下返回注册的cache。

传入数据类型错误，src和dst不匹配情况下会抛出TypeError或ValueError异常。

传入参数为None，会抛出AttributeError异常。

**约束说明**

- Device内存需要先注册再进行建链，Host内存不约束顺序。

- 注册内存地址需自行保证不重复。

- 最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的芯片如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

- D2D HCCS数据传输时，首地址需要按照2MB对齐，否则可能导致link失败，HCCS不支持Host内存。该约束支持的芯片如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

- RDMA数据传输时，申请Host内存必须通过aclrtMallocHost接口，否则可能导致link失败。该约束支持的芯片如下：
<br>- Atlas A2 训练系列产品/Atlas A2 推理系列产品
<br>- Atlas A3 训练系列产品/Atlas A3 推理系列产品

## register\_blocks\_cache

**函数功能**

PagedAttention场景下，调用此接口注册一个自行申请的内存。

**函数原型**

```
register_blocks_cache(cache_desc: CacheDesc, addrs: List[int], blocks_cache_key: Optional[BlocksCacheKey] = None, remote_accessible: Optional[bool] = None) -> Cache:
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_desc | CacheDesc | Cache的描述信息。 |
| addrs | List[int] | Cache的地址。register_cache中的地址个数与register_blocks_cache的地址个数之和不超过240。 |
| blocks_cache_key | Optional[BlocksCacheKey] | 可选的BlocksCacheKey索引。 |
| remote_accessible | Optional[bool] | 指定当前注册内存是否能用来做网络传输，对于Device内存，默认值是True，对于Host内存，默认值是False。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

正常情况下返回注册的cache。

传入数据类型错误，不匹配情况下会抛出TypeError或ValueError异常。

传入参数为None，会抛出AttributeError异常。

**约束说明**

如果通过HCCS进行数据传输，且内存不是通过aclrtMalloc申请，则地址需要按照页大小对齐（如果确定页大小是多少，推荐2MB对齐），否则可能导致link失败。该约束支持的芯片如下：

- Atlas A2 训练系列产品/Atlas A2 推理系列产品
- Atlas A3 训练系列产品/Atlas A3 推理系列产品

Ascend 950PR/Ascend 950DT场景下，无约束。

## transfer\_cache\_async

**函数功能**

异步分层传输Cache。

**函数原型**

```
transfer_cache_async(self,
                     src_cache: Cache,
                     layer_synchronizer: LayerSynchronizer,
                     transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]],
                     src_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_memory_size: Optional[int] = None) -> CacheTask
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| src_cache | Cache | 源Cache。 |
| layer_synchronizer | LayerSynchronizer | LayerSynchronizer的实现类对象。 |
| transfer_configs | Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]] | 传输配置列表或元组。 |
| src_block_indices | Optional[Union[List[int], Tuple[int]]] | 源Cache的block indices，当源Cache为PA场景时设置。 |
| dst_block_indices | Optional[Union[List[int], Tuple[int]]] | 目的Cache的block indices，当目的Cache为PA场景时设置。 |
| dst_block_memory_size | Optional[int] | 目的Cache每个block占用的内存大小，当目的Cache为PA场景时设置。如果源Cache也为PA场景，则可省略该参数，此时会自动将其设置为源Cache每个block占用的内存大小。<br>该参数设置为0时等同于省略该参数。 |

**调用示例**

```
cache_task = cache_manager.transfer_cache_async(cache, LayerSynchronizerImpl(), transfer_configs)
```

**返回值**

正常情况下返回CacheTask。

传入数据类型错误，会抛出TypeError或ValueError异常。

传入数据非法，会抛出LLMException异常。

**约束说明**

- 不支持src\_cache是HOST，dst\_cache是DEVICE的传输场景。
- 不支持src\_cache是PA场景，dst\_cache是非PA场景。
- 不支持单进程多卡场景。
- 若dst\_cache是HOST，仅支持dst\_cache通过allocate\_cache申请方式申请。
- 需要保证transfer\_config中的dst\_addrs的有效性，以及在传输dst\_block\_indices场景下数据的有效性，否则错误未知。
- 开启enable\_remote\_cache\_accessible时，transfer\_configs中的类型需为TransferWithCacheKeyConfig，不开启enable\_remote\_cache\_accessible时，transfer\_configs中的类型需为TransferConfig。

## push\_blocks

**函数功能**

PagedAttention场景下，根据BlocksCacheKey，通过block列表的方式从本地节点推送Cache到远端Cache。

**函数原型**

```
push_blocks(self,
                    dst_cache_key: BlocksCacheKey,
                    src_cache: Cache,
                    src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    src_layer_range: range = None,
                    dst_layer_range: range = None,
                    tensor_num_per_layer = _NUM_TENSORS_PER_LAYER)
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| dst_cache_key | BlocksCacheKey | 远端的Cache索引。 |
| src_cache | Cache | 本地Cache。 |
| src_blocks | Optional[Union[Tuple[int], List[int]]] | 本地的block index列表。 |
| dst_blocks | Union[Tuple[int], List[int]] | 远端的block index列表。 |
| src_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。默认值为None。<br>传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| dst_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。默认值为None。<br>传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| tensor_num_per_layer | Optional[int] | 可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

- 正常情况下无返回值。
- 传入数据类型错误情况下会抛出TypeError或ValueError异常。
- 执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。
- layer\_range参数异常会抛出LLMException异常。

**约束说明**

- 当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为PA的场景。
- 开启enable\_remote\_cache\_accessible时会引入额外约束，详见该配置的约束说明章节。
- 在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池。
- 当前仅支持src\_layer\_range和dst\_layer\_range取值一致。

## push\_cache

**函数功能**

根据CacheKey，从本地节点推送Cache到远端Cache。

**函数原型**

```
push_cache(self,
                   dst_cache_key: CacheKeyByIdAndIndex,
                   src_cache: Cache,
                   src_batch_index: int = 0,
                   src_layer_range: range = None,
                   dst_layer_range: range = None,
                   size: int = -1,
                   tensor_num_per_layer = _NUM_TENSORS_PER_LAYER) -> None
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| dst_cache_key | CacheKeyByIdAndIndex | 远端的Cache索引 |
| src_cache | Cache | 本地的cache |
| src_batch_index | int | 本地的batch index，默认为0。 |
| src_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。默认值为None。<br>传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| dst_layer_range | Optional[range] | 可选参数，用于按层pull kv场景。默认值为None。<br>传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续N个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。N为tensor_num_per_layer的取值，默认为2。 |
| tensor_num_per_layer | Optional[int] | 可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。 |
| size | int | 一个tensor传输的大小，默认值-1表示本地单个KV的大小，暂不支持其他设置。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。

layer\_range参数异常会抛出LLMException异常。

**约束说明**

- 当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为连续cache的场景。
- 开启enable\_remote\_cache\_accessible时，只支持cache\_key类型为CacheKeyByIdAndIndex。
- 在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池。
- 当前仅支持src\_layer\_range和dst\_layer\_range取值一致。

## remap\_registered\_memory

**函数功能**

大模型推理过程中，如果发生内存UCE故障，即返回错误码ACL\_ERROR\_RT\_DEVICE\_MEM\_ERROR，上层框架需要先判断发生该故障的内存是否为KV Cache内存，如果不是，请参考[PyTorch](https://www.hiascend.com/developer/software/ai-frameworks/pytorch)的torch\_npu.npu.restart\_device接口的说明获取并修复内存UCE的错误虚拟地址。如果是KV Cache内存，还需要再调用该接口修复注册给网卡的KV Cache内存。

>![](public_sys-resources/icon-note.gif) **说明：**
>本接口为预留接口，暂不支持。

**函数原型**

```
remap_registered_memory(mem_infos: Union[MemInfo, list[MemInfo]]) -> None
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| mem_infos | Union[MemInfo, list[MemInfo]] | 内存信息，或者内存信息的列表。 |

**调用示例**

```
from llm_datadist import *
role = LLMRole.PROMPT  # 发生故障的角色
cluster_id = 1  # 发生故障的LLMDataDist id
datadist = LLMDataDist(role, cluster_id)
cache_manager = datadist.cache_manager
addr = 12345678 # 发生故障的地址
size = 1024  # 发生故障的地址大小
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, addr, size)
cache_manager.remap_registered_memory(mem_info)
```

**返回值**

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。<br><br>Ascend 950PR/Ascend 950DT不支持该接口。

**约束说明**

当前仅支持Device类型的内存修复。

## unregister\_cache

**函数功能**

解除注册一个自行申请的内存。

**函数原型**

```
unregister_cache(cache_id: int) -> None
```

**参数说明**

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| cache_id | int | 调用register_cache或register_blocks_cache返回的cache的id。 |

**调用示例**

请参考[样例运行](../../examples/python/README.md)。

**返回值**

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

**约束说明**

- 当前仅支持配置local\_comm\_res场景使用。
- 调用该接口之前，需要先调用unlink进行断链，否则会导致HCCL报错。
