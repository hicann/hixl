# 简介

- 支持的形态如下：

  <!-- npu="910b" id1 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，仅支持D2D。
  <!-- end id1 -->
  <!-- npu="A3" id2 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。
  <!-- end id2 -->
  <!-- npu="950" id3 -->
  - Ascend 950PR/Ascend 950DT：超节点内使用UB协议，超节点间使用RoCE协议。
  <!-- end id3 -->
- 当前支持Python 3.9–3.14，Python安装方法请参考[Python官网](https://www.python.org/)。
  <!-- npu="A3,910b" id4 -->
- 最大注册50GB的Device内存。HDK版本低于25.5时，最大注册20GB的Host内存；HDK版本大于等于25.5时，最大注册1TB的Host内存。注册内存越大，占用的OS内存越多。该约束支持的形态如下：
  <!-- npu="910b" id5 -->
  - Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。
  <!-- end id5 -->
  <!-- npu="A3" id6 -->
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品
  <!-- end id6 -->
  <!-- end id4 -->
