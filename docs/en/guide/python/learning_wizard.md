# Learning Guide

<!-- md-trans-meta sourceCommit=c9bc8b9d0daa461eb6353a81395e50c4ee665717 translatedAt=2026-10-21T07:15:41.612Z pushedAt=2026-10-27T02:46:32.687Z nl=2 -->

## Intended Audience

This document guides developers through using Python APIs of the one-sided communication library's LLM-DataDist component for point-to-point data transfers, building disaggregated large-model inference frameworks, and improving throughput of the large-model inference system.

Readers need knowledge in the following areas to better understand this document:

- Basic large-model architectures and concepts
- Familiarity with large-model inference processes
- Ability to develop programs in Python
- Experience running large-model inference with frameworks such as vLLM

## Reading Guide

If you are new to LLM-DataDist or this document, use Table 1 to understand its contents and find relevant sections.

**Table 1** Reading Guide

|Scenario|Section|Description|
|--|--|--|
|Understand LLM-DataDist and its capabilities|[Overview](./introduction.md)|LLM-DataDist concepts, purpose, advantages, and scenarios.|
|Try developing a disaggregated large-model inference framework with LLM-DataDist|[Quick Start](./quick_start.md)|Overall development process, environment preparation, and a complete executable reference example for building disaggregated large-model inference frameworks with LLM-DataDist.|
|Understand LLM-DataDist API capabilities|[Features](./functions.md)|LLM-DataDist API functionality and usage examples.|
|Learn related background information|[Appendices](./appendices.md)|Additional information on basic concepts and large-model inference processes.|
|Troubleshoot application development with LLM-DataDist|[LLMStatusCode](../../api/python/LLMStatusCode.md)|API error codes, cause analysis, and solutions.|
