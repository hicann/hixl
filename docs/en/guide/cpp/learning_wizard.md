# Learning Guide

<!-- md-trans-meta sourceCommit=c9bc8b9d0daa461eb6353a81395e50c4ee665717 translatedAt=2026-10-21T07:15:41.607Z pushedAt=2026-10-27T02:46:32.601Z nl=2 -->

## Intended Audience

This document guides developers through using the one-sided communication library's C++ APIs for inter-cluster data transfers to build disaggregated large-model inference frameworks and improve throughput.

Readers need knowledge in the following areas to better understand this document:

- Basic large-model architectures and concepts
- Familiarity with large-model inference processes
- Ability to develop programs in C++
- Experience running large-model inference with an inference framework

## Reading Guide

If you are new to the one-sided communication library or this document, use Table 1 to understand its contents and find relevant sections.

**Table 1** Reading Guide

|Scenario|Section|Description|
|--|--|--|
|Understand the one-sided communication library and its capabilities|[Overview](introduction.md)|Basic concepts, purpose, advantages, and scenarios of the one-sided communication library.|
|Try developing a disaggregated large-model inference framework with the one-sided communication library|[Quick Start](hixl_develop.md#quick-start)|Overall development process, environment preparation, and a simple reference example for building a disaggregated large-model inference framework with the one-sided communication library.|
|Understand the one-sided communication library's API capabilities|[Features](hixl_develop.md#features)|API functionality and implementation examples.|
|Explore additional library features|[Advanced Topics](llm-datadist_develop.md#advanced-topics)|Advanced features such as role switching for disaggregated large-model inference frameworks.|
|Learn related background information|[Appendices](appendices.md)|Additional information on basic concepts and large-model inference.|
|Troubleshoot application development with the one-sided communication library|[HIXL Error Codes](../../api/cpp/HIXL-error-code.md)|API error codes, cause analysis, and solutions.|
