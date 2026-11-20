# Overview

<!-- md-trans-meta sourceCommit=9524869fa42b724c5fa4b2e3cc6ab5e310ab84a6 translatedAt=2026-10-21T07:15:41.568Z pushedAt=2026-10-27T02:46:32.624Z nl=2 -->

The one-sided communication library provides simple, reliable, efficient point-to-point transfers for clusters through easy-to-use APIs, bridging AI applications and multiple transfer links. It supports large-model PD disaggregation, parameter switching in RL post-training, model parameter caching, and other scenarios.
<br>This development guide describes how to use the library's APIs for inter-cluster data transfers and build PD-disaggregated large-model inference frameworks.

**Table 1** Scenarios

|Guide|Overview|
|--|--|
|One-Sided Communication Library (C++)|Connection management, memory management, and transfers through C++ HIXL APIs. For distributed memory pools, HIXL provides transfers based solely on local and remote addresses.<br>Supports D2D, D2H, and H2D.<br>Also covers connection and KV Cache management through C++ LLM-DataDist APIs, supporting one-sided connection establishment. Decode and Prompt can pull and push Caches in both directions.|
|One-Sided Communication Library (Python)|Connection and KV Cache management in CacheManager mode through Python LLM-DataDist APIs. Supports one-sided and two-sided connection establishment, with all LLM-DataDist instances initiating simultaneously for the latter. Decode and Prompt can pull and push Caches in both directions.<br>Supports D2D, D2H, and H2D.|
