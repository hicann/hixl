# 接口参考（C++）

## 简介

-   LLM-DataDist
    -   LLM-DataDist相关接口存放在："INSTALL\_DIR\/include/llm\_datadist/llm\_datadist.h"。INSTALL\_DIR请替换为CANN软件安装后文件存储路径。若安装的Ascend-cann-toolkit软件包，以root安装举例，则安装后文件存储路径为：/usr/local/Ascend/latest。
    -   LLM-DataDist接口对应的库文件是：libllm\_datadist.so。

-   HIXL：Huawei Xfer Library
    -   HIXL相关接口存放在："INSTALL\_DIR/include/hixl/hixl.h"。INSTALL\_DIR请替换为CANN软件安装后文件存储路径。若安装的Ascend-cann-toolkit软件包，以root安装举例，则安装后文件存储路径为：/usr/local/Ascend/latest。
    -   HIXL接口对应的库文件是：libcann_hixl.so。

支持的产品形态如下：

-   Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，LLM-DataDist相关接口仅支持D2D。
-   Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，LLM-DataDist相关接口不支持Host内存作为远端Cache。

## 接口列表

接口列表如下：
-  [LLM-DataDist接口](LLM-DataDist接口.md)
-  [LLM-DataDist数据结构](LLM-DataDist数据结构.md)
-  [LLM-DataDist错误码](LLM-DataDist错误码.md)
-   [HIXL接口](HIXL接口.md)
-  [HIXL数据结构](HIXL数据结构.md)
-  [HIXL错误码](HIXL错误码.md)
-   [不支持的接口](不支持的接口.md)

