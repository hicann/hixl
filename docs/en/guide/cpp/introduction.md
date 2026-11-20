# Overview

<!-- md-trans-meta sourceCommit=03e9ed73888114c36d552c09b173ef2aade11561 translatedAt=2026-10-27T01:31:10.178Z pushedAt=2026-10-27T02:46:32.590Z nl=2 -->

## One-Sided Communication Library Overview

The one-sided communication library provides simple, reliable, efficient point-to-point transfers for clusters through easy-to-use APIs, bridging AI applications and multiple transfer links. It supports large-model PD disaggregation, parameter switching in RL post-training, model parameter caching, and other scenarios.

**Key Advantages**

- One-Sided Zero-Copy communication: Simple, reliable APIs transfer local data directly to remote memory once ready, without remote node operations. This supports scheduling that overlaps communication and computation. Zero-copy transfers directly between user memory regions avoid redundant data movement, reducing memory bandwidth usage and memory consumption.
- Hardware abstraction and multi-link compatibility for high-speed interconnection across devices: The library hides differences between Ascend chip architectures, avoiding chip-specific code adaptation. Native support for RDMA, HCCS, and other high-speed protocols provides bandwidth up to 119GB/s, enabling seamless cross-architecture interconnection with low latency and high throughput.
- Minimalist API design and deep integration with open-source ecosystem frameworks: The API set is streamlined to just over 10 core calls, lowering the integration barrier for developers. It provides comprehensive C++/Python support. The library deeply integrates with Mooncake and DeepLink, while inference engines such as vLLM and SGLang can call its APIs directly for efficient cross-device KV Cache transfers, reducing memory access latency during large-model inference by 20% and improving inference throughput.

**Software Architecture**

Figure 1 shows the one-sided communication library.

**Figure 1** One-Sided Communication Library Software Architecture
![One-Sided Communication Library Software Architecture](figures/architecture.png "One-sided communication library software architecture")

**Core Components**

- HIXL Engine: Core transfer engine with basic APIs supporting D2D, D2H, H2D, and protocols including HCCS and RDMA. Provides fast, reliable transfers over multiple data links for homogeneous and heterogeneous clusters. Efficiently adapts connections and schedules resources during dynamic scaling, providing reliable communication for the cluster.
- LLM-DataDist: Built on HIXL Engine, provides data transfer APIs with KV Cache semantics for quick, flexible integration with inference engines such as vLLM and SGLang.

## One-Sided Communication Library Scenarios

Build a PD-disaggregated large-model inference framework using the one-sided communication library.

In large-model inference, Prefill feeds the user's Prompt into the model for computation, stores intermediate results in KV Cache, and generates the 1st token. During Decode, the previous token is fed into the model, and the previously generated KV Cache is read from device memory for further computation. For the large-model inference process based on KV Cache, see [Overview of Large-Model Inference](appendices.md#overview-of-large-model-inference).

PD-disaggregated inference deploys Prefill and Decode on clusters with different specifications and architectures to improve performance and resource utilization. This increases throughput; see [Why Use PD Disaggregation?](appendices.md#why-use-pd-disaggregation).

In PD-disaggregated inference, the KV Cache generated during Prefill needs to be transferred to Decode for incremental iterative inference. As a distributed cluster and data management component for large models, the one-sided communication library exposes simple APIs to users. The PD-disaggregated large-model inference framework built with the one-sided communication library is shown below. The one-sided communication library provides KV Cache transfer and connection management between Prefill Node and Decode Node.![260107091904908](figures/260107091904908.png)
