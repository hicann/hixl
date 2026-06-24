# 附录

## 基本概念

**表1**  基本概念

|概念|含义|
|--|--|
|Prefill阶段|也叫Prompt或者全量推理阶段，是对用户传入的输入第一次进行推理的阶段。该阶段的输入是用户传入的完整长度的语句。|
|Decode阶段|也叫增量推理阶段，基于Prefill阶段的输出进行迭代推理的阶段。由于该阶段每次迭代的输入都是上一次推理的输出，所以叫做增量推理。|
|PD分离|也就是Prefill阶段和Decode阶段分离执行。|
|KV Cache|KV Cache其实是在大语言模型中使用的一种技术，用于缓存已经计算过的**Key**和**Value**，避免在每一步生成过程中重复计算整个上下文。本文代指计算完的Key和Value缓存。|
|PagedAttention|简称PA。是一种使用离散block的方式管理请求KV Cache的功能，相比未使用此功能的框架而言，能够达到节省KV Cache内存占用大小的效果。|
|block_table|在PA场景下，表示请求KV Cache占用的block index的集合。|
|请求|可以理解为用户提的问题，该问题经过Prefill和Decode阶段会有对应的回答，我们称这个过程为一次请求。|
|role|表示LLM-DataDist代表的业务角色。业务角色分为Prefill和Decoder两种。|
|cluster_id|表示LLM-DataDist所在的集群标识。主要用于Decode侧通过cluster_id找到对应链路，从而找到Prefill侧缓存的对应请求的KV Cache。对于一个模型的不同切分部署到不同Device的场景，当前在支持PD对等切分场景下，cluster_id在不同切分模型间设置为相同。|
|集群动态扩缩容|根据业务闲忙，动态的调整集群的数量以及PD集群的配比，从而达到闲时节省资源，忙时提高吞吐的目的。|
|D2D传输|指数据从Device设备往Device设备传输。|
|D2H传输|指数据从Device设备往Host设备传输。|
|H2D传输|指数据从Host设备往Device设备传输。|
|D2RH|指数据从Device设备往远端Host设备传输。|
|RH2D|指数据从远端Host设备往Device设备传输。|
|TTFT|Time To First TokenLLM推理的一个指标。指从输入到输出第一个token的延迟。即当一批用户进入到推理系统之后，用户完成Prefill阶段的过程需要花费的时间。这也是系统生成第一个字符所需的响应时间。|
|TBT|Time Between Tokenstoken间时延，指的是每一个decoding所需要的时长。它反映的是大语言模型系统在线上处理的过程中，每生成一个字符的间隔是多长时间，也就是生成的过程有多么流畅。|
|Continuous Batching|连续批处理技术，是一种在大模型推理中优化计算效率的方法。它通过将多个推理请求组合成一个批次进行处理，从而充分利用计算资源，提高推理速度和吞吐量。这种技术的核心思想在于减少计算过程中的空闲时间，实现计算资源的最大化利用。|
|单边建链|Client向Server发起建链。|
|双边建链|所有LLM-DataDist可以同时发起建链。|

## 大模型推理流程简介

ChatGPT的推出意味着交互式人工智能逐渐成为商业化成熟产品，同时也进一步推动了底层技术大型语言模型LLM的研究和进步。现有主要使用的LLM包括GPT系列、LLAMA系列、GLM系列。其模型基础架构都采用Transformer架构，并以Decode-Only为主。该类Transformer-Based-Decode-Only的LLM在推理预测时，采用自回归生成（auto-regressive generation，AR）模式。即每个token生成需要经过LLM模型的前向推理过程，即完成N次Transformer Layer计算，意味着包含M个token的语句完整生成需要经过M次完整LLM前向推理过程。

在Transformer推理过程中利用KV Cache技术可降低Decoding阶段的计算量，目前已成为LLM推理系统的必选技术。采用KV Cache的LLM推理过程通常分为预填充（Prefill）和解码（Decode）两个阶段。

- Prefill阶段：将用户请求的Prompt传入大模型，进行计算，中间结果写入KV Cache并推理出第1个token。随着Prompt Sequence Length长度线性增长，对首token时延（TTFT）指标有要求的业务通常采用单batch方式执行LLM推理，该阶段属于计算密集型操作。
- Decode阶段：将请求的前1个token传入大模型，从显存读取前文产生的KV Cache再进行计算。采用KV Cache技术后，单token计算量低，而主要瓶颈在于搬运参数量，故通常需要采用多batch方式提升利用率，该阶段属于访存密集型操作。

![](figures/260109142344517.png)

## 为什么要做PD分离

从实现上来看，整个LLM推理过程由Prefill和多轮迭代的Decode组成，想要在Decode阶段实现Continuous batching的前提是，每个被调度的request需要有空闲算力完成Prefill计算。按现有的部署模式，Prefill和Decode部署在一起，当有新的Prefill请求时，会被优先处理，从而导致Decode的执行流程由于增量token时延（TBT）而无法得到有效保障。例如下图，当request5或request6到来时，系统可能会优先执行request5或request6的Prefill，此时request2/3/4的响应时延会受到一定影响，从而导致TBT不稳定。

![](figures/260109142618323.png)

在实际的深度学习模型部署中，由于Prefill和Decode两阶段的计算/通信特征的差异特点，为了提升性能和资源利用效率，通过PD分离部署方案将Prefill和Decode分别部署在不同规格和架构的集群中，并且配合服务层的任务调度，在满足TTFT和TBT指标的前提下，结合Continuous batching机制尽可能提高Decode阶段的batch并发数，提升算力利用率。

基于该方案，结合下图可以看到Prefill和Decode的执行互不影响，系统能够提供给用户一个稳定的TBT。

![](figures/260109142712834.png)
