
# 简介

- LLM-DataDist
  - LLM-DataDist相关接口存放在："INSTALL\_DIR\/include/llm\_datadist/llm\_datadist.h"。INSTALL\_DIR请替换为CANN软件安装后文件存储路径。若安装的Ascend-cann-toolkit软件包，以root安装举例，则安装后文件存储路径为：/usr/local/Ascend/cann。
  - LLM-DataDist接口对应的库文件是：libllm\_datadist.so。

- HIXL：Huawei Xfer Library
  - HIXL相关接口存放在："INSTALL\_DIR/include/hixl/hixl.h"。INSTALL\_DIR请替换为CANN软件安装后文件存储路径。若安装的Ascend-cann-toolkit软件包，以root安装举例，则安装后文件存储路径为：/usr/local/Ascend/cann。
  - HIXL接口对应的库文件是：libcann_hixl.so。
  <!-- npu="950" id1 -->
  - HIXL_CS相关接口存放在："INSTALL\_DIR/include/cs/hixl_cs.h"。INSTALL\_DIR请替换为CANN软件安装后文件存储路径。若安装的Ascend-cann-toolkit软件包，以root安装举例，则安装后文件存储路径为：/usr/local/Ascend/cann。该接口仅支持Ascend 950PR/Ascend 950DT。
  <!-- end id1 -->
  <!-- npu="950" id2 -->
  - HIXL_CS接口对应的库文件是：libcann_hixl.so。仅支持Ascend 950PR/Ascend 950DT。
  <!-- end id2 -->

支持的产品形态如下：

<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，LLM-DataDist相关接口仅支持D2D。
<!-- end id3 -->
<!-- npu="A3" id4 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，LLM-DataDist相关接口不支持Host内存作为远端Cache。
<!-- end id4 -->
<!-- npu="950" id5 -->
- Ascend 950PR/Ascend 950DT：超节点内使用UB协议，超节点间使用RoCE协议。 
<!-- end id5 -->
