# Appendices

<!-- md-trans-meta sourceCommit=03e9ed73888114c36d552c09b173ef2aade11561 translatedAt=2026-10-27T01:31:10.176Z pushedAt=2026-10-27T02:46:32.561Z nl=2 -->

## Basic Concepts

**Table 1** Basic Concepts

|Concept|Meaning|
|--|--|
|Prefill Phase|Also called the Prompt or full-inference phase. Performs the first inference on the user's input, using the complete input sequence.|
|Decode Phase|Also called incremental inference. Iteratively infers from the Prefill output. Each iteration takes the previous inference output as input, hence incremental inference.|
|PD Disaggregation|Executing the Prefill and Decode phases separately.|
|KV Cache|KV Cache caches previously computed **Key** and **Value** tensors in large language models, avoiding recomputation of the entire context at each generation step. Here, it refers to the computed Key and Value cache.|
|PagedAttention|Abbreviated PA. Manages request KV Cache using discrete blocks, reducing KV Cache memory usage compared with frameworks without this feature.|
|block_table|In PA scenarios, the set of block indices occupied by a request's KV Cache.|
|Request|A user question processed through Prefill and Decode to produce an answer. This complete process is a request.|
|role|LLM-DataDist application role: Prefill or Decoder.|
|cluster_id|Cluster identifier of LLM-DataDist. Decode primarily uses `cluster_id` to locate the connection and the corresponding request's KV Cache on Prefill. When model partitions are deployed on different Devices, supported symmetric PD partitioning uses the same `cluster_id` across model partitions.|
|Dynamic Cluster Scaling|Dynamically adjusts cluster count and the PD cluster ratio based on workload, saving resources at low load and increasing throughput at high load.|
|D2D Transfer|Transfers data from Device to Device.|
|D2H Transfer|Transfers data from Device to Host.|
|H2D Transfer|Transfers data from Host to Device.|
|D2RH|Transfers data from Device to remote Host.|
|RH2D|Transfers data from remote Host to Device.|
|TTFT|Time To First Token, an LLM inference metric measuring latency from input to the first output token. For a batch of users entering the system, it is the time to complete Prefill and produce the first character.|
|TBT|Time Between Tokens, the duration of each decoding step. Measures the interval between generated characters during online processing, reflecting generation smoothness.|
|Continuous Batching|Continuous batching improves large-model inference efficiency by grouping multiple requests into a batch, increasing inference speed and throughput. It minimizes idle time to maximize computing resource utilization.|
|One-Sided Connection Establishment|The Client initiates a connection to the Server.|
|Two-Sided Connection Establishment|All LLM-DataDist instances may initiate connections simultaneously.|

## Overview of Large-Model Inference

ChatGPT marked the maturation of interactive AI into commercial products and further advanced research on large language models (LLMs). Common LLMs include GPT, LLAMA, and GLM families, whose underlying architectures all use Transformer, primarily in decoder-only form. During inference, these models use autoregressive generation (AR): each token requires an LLM forward pass with N Transformer Layer computations. Generating a sequence of M tokens therefore requires M complete LLM forward passes.

KV Cache reduces Decode computation during Transformer inference and has become essential to LLM inference systems. KV Cache-based inference typically comprises Prefill and Decode phases.

- Prefill: Feeds the user's Prompt into the large model for computation, writes intermediate results to KV Cache, and generates the 1st token. As the Prompt Sequence Length grows linearly, applications with time to first token (TTFT) requirements typically use a single batch for LLM inference. This phase is compute-intensive.
- Decode: Feeds the previous token into the model and reads the previously generated KV Cache from device memory. With KV Cache, per-token computation is low and parameter transfer is the main bottleneck, so multiple batches typically improve utilization. This phase is memory-intensive.

![260109142344517](figures/260109142344517.png)

## Why Use PD Disaggregation?

From an implementation perspective, LLM inference consists of Prefill and repeated Decode iterations. Continuous batching in Decode requires spare compute to complete Prefill for each scheduled request. With colocated Prefill and Decode, new Prefill requests receive priority, making Decode time between tokens (TBT) difficult to guarantee. For example, in the following figure, when request5 or request6 arrives, the system may prioritize that request's Prefill, affecting request2/3/4 response latency and destabilizing TBT.

![260109142618323](figures/260109142618323.png)

In actual deep learning model deployment, PD disaggregation deploys Prefill and Decode on clusters with different specifications and architectures because of their different computation and communication characteristics, improving performance and resource utilization. Together with service-level task scheduling, continuous batching maximizes Decode batch concurrency and compute utilization while meeting TTFT and TBT requirements.

As shown below, Prefill and Decode execute independently, allowing the system to provide stable TBT.

![260109142712834](figures/260109142712834.png)
