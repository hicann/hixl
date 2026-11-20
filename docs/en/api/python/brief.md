# Overview

<!-- md-trans-meta sourceCommit=df3b14504e1aa05d42a2e7708d83c859f02821d5 translatedAt=2026-11-19T01:01:42.804Z pushedAt=2026-11-19T01:04:51.528Z nl=2 -->

- The following products are supported:

  <!-- npu="910b" id1 -->
  - Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. Only D2D is supported when the Server uses the HCCS transport protocol in this scenario.
  <!-- end id1 -->
  <!-- npu="A3" id2 -->
  - Atlas A3 products: Host memory is unsupported as a remote Cache when HCCS is used.
  <!-- end id2 -->
  <!-- npu="950" id3 -->
  - Ascend 950PR&950DT products: Use UB within a supernode and RoCE between supernodes.
  <!-- end id3 -->
- The [package released on the Ascend website](https://www.hiascend.com/cann/download) supports Python 3.9–3.14. You can use it directly with the Python version used to precompile the package. For other versions, the first import of `hixl` automatically compiles a compatible extension and caches it. For required dependencies, see [Quick Start](../../guide/python/quick_start.md). To install from source, see [Building from Source](../../build.md). For Python installation instructions, see the [Python website](https://www.python.org/).
  <!-- npu="A3,910b" id4 -->
- Up to 50GB of Device memory can be registered. The Host memory limit is 20GB for HDK earlier than 25.5 and 1TB for HDK 25.5 or later. Registering more memory consumes more OS memory. This constraint applies to the following product forms:
  <!-- npu="910b" id5 -->
  - Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
  <!-- end id5 -->
  <!-- npu="A3" id6 -->
  - Atlas A3 products
  <!-- end id6 -->
  <!-- end id4 -->
