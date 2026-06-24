# 概述

## LLM-DataDist简介

在大模型推理场景下，随着模型batch size的增大，Prefill阶段的性能会线性降低，Decode阶段会额外占用更多的内存。两阶段对资源的需求不同，部署在一起导致资源分配不均，成本居高不下。通过LLM-DataDist构建的大模型推理分离式框架有效地解决了该问题。在分离式框架中，将Prefill和Decode分别部署在不同规格和架构的集群中，提升了性能和资源利用效率，提升了大模型推理系统吞吐量。

LLM-DataDist作为大模型分布式集群和数据管理组件，提供了高性能、零拷贝的点对点数据传输的能力，该能力通过简易的API开放给用户。LLM-DataDist利用昇腾集群多样化通信链路（RoCE/HCCS/UB），可实现跨实例和集群的高效KV Cache传输，支持与主流LLM推理框架vLLM等的集成，并可用于构筑分布式数据管理系统。LLM-DataDist功能主要包括：链路管理和缓存管理。

- 链路管理用于集群之间建链、断链，实现集群的动态扩缩的能力。
- 缓存管理用于管理KV Cache，提供PD（下文P侧代表Prefill, D侧代表Decode）之间点对点传输KV Cache的能力。

![](figures/260109143238748.png)

## LLM-DataDist应用场景

通过LLM-DataDist构建大模型推理PD分离式框架。

在大模型推理中，Prefill阶段将用户请求Prompt传入大模型进行计算，中间结果写入KV Cache并输出第1个token。在Decode阶段中，将请求的前1个token传入大模型，从显存读取之前产生的KV Cache再进行计算。基于KV Cache的大模型推理过程请参见[大模型推理流程简介](appendices.md#大模型推理流程简介)。

在大模型推理PD分离式框架中，为了提升性能和资源利用效率，将Prefill和Decode分别部署在不同规格和架构的集群中。PD分离式框架可提升大模型推理系统吞吐量，详见[为什么要做PD分离](appendices.md#为什么要做pd分离)。

大模型推理PD分离式框架中，Prefill阶段生成的KV Cache需要传输到Decode，然后Decode阶段进行增量迭代推理。LLM-DataDist作为大模型分布式集群和数据管理组件，通过简易的API开放给用户，构建大模型推理PD分离式框架如下图所示，LLM-DataDist提供了Prefill Node和Decode Node之间的KV Cache传输及链路管理。

![](figures/260107091904908.png)
