
# Overview<a id="Overview"></a>

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.569Z pushedAt=2026-10-27T02:46:32.090Z nl=2 -->

The following products are supported.

<!-- npu="910b" id3 -->
- Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. When the Server uses HCCS in this scenario, the LLM-DataDist APIs support only D2D.
<!-- end id3 -->
<!-- npu="A3" id4 -->
- Atlas A3 products: When HCCS is used in this scenario, the LLM-DataDist APIs do not support Host memory as a remote Cache.
<!-- end id4 -->
<!-- npu="950" id5 -->
- Ascend 950PR&950DT products: Supported protocols within a supernode include UB, RoCE, UB_RTP, and UBoE. Supported protocols between supernodes include RoCE, UB_RTP, and UBoE.
<!-- end id5 -->
