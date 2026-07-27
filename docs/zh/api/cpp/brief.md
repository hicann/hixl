
# 简介

本文档支持的产品形态如下：

<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，LLM-DataDist相关接口仅支持D2D。
<!-- end id3 -->
<!-- npu="A3" id4 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，LLM-DataDist相关接口不支持Host内存作为远端Cache。
<!-- end id4 -->
<!-- npu="950" id5 -->
- Ascend 950PR/Ascend 950DT：超节点内支持的协议包括：UB、RoCE、UBG、UBoE，超节点间支持的协议包括：RoCE、UBG、UBoE。
<!-- end id5 -->
