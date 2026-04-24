# 接口参考（Python）

- 支持的形态如下：

  - Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，仅支持D2D。最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越大。
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越大。
  - Ascend 950PR/Ascend 950DT：超节点内使用UB协议，超节点间使用RoCE协议。 
- 当前支持Python3.9~12，Python安装方法请参考[Python官网](https://www.python.org/)。
- 最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的形态如下：
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品

接口列表如下：

- [LLMDataDist](LLMDataDist.md)
- [LLMConfig](LLMConfig.md)
- [CacheManager](CacheManager.md)
- [Cache](Cache.md)
- [LLMRole](LLMRole.md)
- [LLMClusterInfo](LLMClusterInfo.md)
- [RegisterMemStatus](RegisterMemStatus.md)
- [Placement](Placement.md)
- [CacheDesc](CacheDesc.md)
- [MemType](MemType.md)
- [MemInfo](MemInfo.md)
- [CacheKey](CacheKey.md)
- [CacheKeyByIdAndIndex](CacheKeyByIdAndIndex.md)
- [BlocksCacheKey](BlocksCacheKey.md)
- [LayerSynchronizer](LayerSynchronizer.md)
- [TransferConfig](TransferConfig.md)
- [TransferWithCacheKeyConfig](TransferWithCacheKeyConfig.md)
- [CacheTask](CacheTask.md)
- [LLMException](LLMException.md)
- [LLMStatusCode](LLMStatusCode.md)
- [DataType](DataType.md)
- [待废弃](待废弃.md)
