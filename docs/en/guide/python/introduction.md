# Overview

<!-- md-trans-meta sourceCommit=03e9ed73888114c36d552c09b173ef2aade11561 translatedAt=2026-10-27T01:31:10.181Z pushedAt=2026-10-27T02:46:32.675Z nl=2 -->

## LLM-DataDist Overview

In large-model inference, as model batch size increases, Prefill performance decreases linearly while Decode consumes additional memory. Their different resource needs make colocation inefficient and costly. The disaggregated inference framework built with LLM-DataDist effectively addresses this problem by deploying Prefill and Decode on clusters with different specifications and architectures to improve performance, resource utilization, and throughput.

LLM-DataDist is a distributed cluster and data management component providing high-performance, zero-copy point-to-point transfers through simple APIs. It uses Ascend cluster communication links (RoCE/HCCS/UB) for efficient cross-instance and cross-cluster KV Cache transfers, integrates with mainstream frameworks such as vLLM, and supports distributed data management systems. Its main features are connection management and cache management.

- Connection management establishes and disconnects inter-cluster connections for dynamic scaling.
- Cache management manages KV Cache and provides point-to-point KV Cache transfers between P (Prefill) and D (Decode).

![260109143238748](figures/260109143238748.png)

## LLM-DataDist Scenarios

Build a PD-disaggregated large-model inference framework using LLM-DataDist.

In large-model inference, Prefill feeds the user's Prompt into the model for computation, stores intermediate results in KV Cache, and outputs the 1st token. During Decode, the previous token is fed into the model, and the previously generated KV Cache is read from device memory for further computation. For the large-model inference process based on KV Cache, see [Overview of Large-Model Inference](appendices.md#overview-of-large-model-inference).

PD disaggregation deploys Prefill and Decode on clusters with different specifications and architectures to improve performance and resource utilization. This increases throughput; see [Why Use PD Disaggregation?](appendices.md#why-use-pd-disaggregation).

In the PD-disaggregated framework for large-model inference, the KV Cache generated during Prefill needs to be transferred to Decode for incremental iterative inference. LLM-DataDist, a distributed cluster and data management component for large models, exposes simple APIs to users. The PD-disaggregated inference framework built with LLM-DataDist is shown below. LLM-DataDist provides KV Cache transfer and connection management between Prefill Node and Decode Node.

![260107091904908](figures/260107091904908.png)
