<div align="center">

# HIXL

<h4>面向集群场景提供简单、可靠、高效的点对点数据传输能力</h4>

[![Documentation](https://img.shields.io/badge/docs-HIXL书架-brightgreen)](https://gitcode.com/cann/hixl/blob/master/docs/README.md)
[![SIG](https://img.shields.io/badge/SIG-hccl-yellow)](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)
[![meeting](https://img.shields.io/badge/meeting-社区会议-yellow)](https://meeting.osinfra.cn/cann?sig=sig-hccl)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-贡献指南-teal)](https://gitcode.com/cann/hixl/blob/master/CONTRIBUTING.md)
[![WeChat](https://img.shields.io/badge/contact_us-HIXL微信交流群-blue)](https://gitcode.com/cann/hixl/blob/master/docs/figures/HIXL_contact.png)
[![zread](https://img.shields.io/badge/Ask_Zread-_.svg?style=flat&color=00b0aa&labelColor=000000&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB3aWR0aD0iMTYiIGhlaWdodD0iMTYiIHZpZXdCb3g9IjAgMCAxNiAxNiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTQuOTYxNTYgMS42MDAxSDIuMjQxNTZDMS44ODgxIDEuNjAwMSAxLjYwMTU2IDEuODg2NjQgMS42MDE1NiAyLjI0MDFWNC45NjAxQzEuNjAxNTYgNS4zMTM1NiAxLjg4ODEgNS42MDAxIDIuMjQxNTYgNS42MDAxSDQuOTYxNTZDNS4zMTUwMiA1LjYwMDEgNS42MDE1NiA1LjMxMzU2IDUuNjAxNTYgNC45NjAxVjIuMjQwMUM1LjYwMTU2IDEuODg2NjQgNS4zMTUwMiAxLjYwMDEgNC45NjE1NiAxLjYwMDFaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00Ljk2MTU2IDEwLjM5OTlIMi4yNDE1NkMxLjg4ODEgMTAuMzk5OSAxLjYwMTU2IDEwLjY4NjQgMS42MDE1NiAxMS4wMzk5VjEzLjc1OTlDMS42MDE1NiAxNC4xMTM0IDEuODg4MSAxNC4zOTk5IDIuMjQxNTYgMTQuMzk5OUg0Ljk2MTU2QzUuMzE1MDIgMTQuMzk5OSA1LjYwMTU2IDE0LjExMzQgNS42MDE1NiAxMy43NTk5VjExLjAzOTlDNS42MDE1NiAxMC42ODY0IDUuMzE1MDIgMTAuMzk5OSA0Ljk2MTU2IDEwLjM5OTlaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik0xMy43NTg0IDEuNjAwMUgxMS4wMzg0QzEwLjY4NSAxLjYwMDEgMTAuMzk4NCAxLjg4NjY0IDEwLjM5ODQgMi4yNDAxVjQuOTYwMUMxMC4zOTg0IDUuMzEzNTYgMTAuNjg1IDUuNjAwMSAxMS4wMzg0IDUuNjAwMUgxMy43NTg0QzE0LjExMTkgNS42MDAxIDE0LjM5ODQgNS4zMTM1NiAxNC4zOTg0IDQuOTYwMVYyLjI0MDFDMTQuMzk4NCAxLjg4NjY0IDE0LjExMTkgMS42MDAxIDEzLjc1ODQgMS42MDAxWiIgZmlsbD0iI2ZmZiIvPgo8cGF0aCBkPSJNNCAxMkwxMiA0TDQgMTJaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00IDEyTDEyIDQiIHN0cm9rZT0iI2ZmZiIgc3Ryb2tlLXdpZHRoPSIxLjUiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIvPgo8L3N2Zz4K&logoColor=ffffff)](https://zread.ai/hicann/hixl)
</div>

## 🔥Latest News

- [2026/04] HIXL支持Device UBoE，详见[Request](https://gitcode.com/cann/hixl/pull/365); HIXL跨代际能力增强，支持A2/A3/A5异构 [[Issue #138](https://gitcode.com/cann/hixl/issues/138)]，[[Issue #115](https://gitcode.com/cann/hcomm/issues/115)]。
- [2026/03] HIXL已支持超节点内FabricMem模式，详见 [FabricMem](docs/FabricMem.md)。
- [2026/01] LLM-DataDist与HIXL已支持下一代芯片的Host RoCE传输能力。
- [2025/12] 基于A3超平面实现D2rH直传；新增链路池与IPv6支持，并对接Mooncake社区的下一代TENT架构。
- [2025/11] HIXL提供异步传输能力，支持更高并发的非阻塞数据传输场景。
- [2025/10] HIXL项目开源，在昇腾芯片上提供高效的多链路D2D/D2H/H2D等单边通信能力，并优化了小数据量批量传输场景的性能。同时HIXL已对接多个开源社区，包含[Mooncake](https://github.com/kvcache-ai/Mooncake/issues/719)，[DeepLink](https://github.com/DeepLink-org/DLSlime/pull/21)等。


## 🚀概述
HIXL（Huawei Xfer Library）是一个灵活、高效的昇腾单边通信库，面向集群场景提供简单、可靠、高效的点对点数据传输能力，并通过简易API开放给用户, 在多AI应用和多传输链路之间建立了桥梁。可用于构建大模型PD分离、RL后训练参数切换、模型参数缓存等多种业务场景。

**核心优势**
- **支持单边零拷贝（One-Sided Zero-Copy）通信机制**：HIXL提供简易、可靠的单边通信接口，可在本地内存数据准备就绪之后，通过单边操作完成向远端内存的直接数据传输。该机制无需远端节点执行任何操作，为用户构建通信与计算重叠掩盖的调度机制提供核心技术支撑。同时，零拷贝能力实现用户内存间的直接数据传输，避免冗余数据搬运，不仅可以降低内存带宽占用，还可以减少内存容量消耗。
- **屏蔽硬件差异，兼容多链路实现跨设备高速互联**：HIXL屏蔽了昇腾系列芯片的底层硬件差异，用户无需针对不同芯片架构进行代码适配。在通信链路层面，该技术原生支持RDMA，HCCS等多种高速互联协议，通信带宽最高可达119GB/s，可实现跨架构设备（如A2系列与A3系列昇腾芯片）的无缝高速互联，满足低时延、 高吞吐的需求。
- **极简API设计，深度适配开源生态框架**：HIXL采用极简式API接口设计，接口数量精简至10余个核心调用，降低开发者集成门槛，同时提供完善的C++/Python语言接口支持。目前已实现与Mooncake、DeepLink等开源框架的深度集成，vLLM、SGLang等主流推理引擎也可以直接调用HIXL API完成KV Cache的跨设备高效传输，将大模型推理过程中的内存访问延迟降低20%，显著提升推理吞吐。

<img src="docs/figures/architecture.png" alt="架构图">


**核心组件**

- **HIXL Engine**：作为核心传输引擎，提供了基础传输接口，支持多种类型内存类型传输，比如D2D、D2H、H2D。同时兼容多种传输协议，包括HCCS、RDMA等。可实现高速、可靠的数据传输。原生支持多类型数据链路，可灵活同构集群、异构集群的复杂场景。面对集群节点动态扩缩容需求时，可快速完成链路适配与资源调度，为集群整体运行构建可靠通信基础。
- **LLM-DataDist**：基于HIXL Engine构建，提供了一套携带KV Cache语义的数据传输接口。可快速、灵活对接vLLM、SGLang等推理引擎。


**性能表现**

在昇腾A3芯片上传输128M数据场景下：
- 通过HCCS链路进行传输，HIXL传输引擎的带宽可达119GB/s
- 通过RDMA链路进行传输，HIXL传输引擎的带宽可达22GB/s

<img src="docs/figures/perf.png" alt="性能数据图">

查看[基准测试Benchmarks](benchmarks/README.md)了解更多细节。


## 🔍目录结构

本项目的关键目录说明如下：

```
├── build.sh                       # 项目工程编译脚本
├── benchmarks                     # 项目benchmark性能用例
├── cmake                          # 项目工程编译目录
├── CMakeLists.txt                 # 项目的CMakeList
├── docs                           # 项目文档介绍
│  ├── cpp                         # C++文档
│  └── python                      # Python文档
├── examples                       # 端到端样例开发和调用示例
│  ├── cpp                         # C++样例
│  ├── python                      # Python样例
├── include                        # 头文件
│  ├── hixl
│  ├── cs
│  ├── adxl
│  └── llm_datadist
├── README.md
├── scripts                        # 脚本路径
│  └── package
├── src                            # 源码路径
│  ├── hixl
│  ├── llm_datadist
│  ├── ops
│  └── python
└── tests                          # 测试工程目录
```


## ⚡️快速入门

若您希望快速体验该组件的构建和样例执行，请访问如下文档获取简易教程。

- [构建](docs/build.md)：介绍组件的编译和安装，包括编译成功后利用tests进行本地验证。
- [样例执行](examples/README.md)：介绍如何端到端执行样例代码，包括C++和Python样例。


## 📖学习教程

若您希望深入了解组件的相关接口并修改源码，请访问如下文档获取详细教程。

- [C/C++接口](docs/cpp/README.md)：C++接口介绍。
- [Python接口](docs/python/README.md)：Python接口介绍。

如果希望深入了解 HIXL 开发指南和技术文章等内容，可参考以下文档：

- [HIXL 资料书架总览](docs/README.md)：汇总HIXL相关资料、技术文章和培训视频链接。

## 🤖 AI Agent支持

- [AI辅助编程](.agents/README.md)：介绍仓内默认使用的一些skills及触发方式，用于提升开发者编程效率和代码质量。
- [Zread智能问答](https://zread.ai/hicann/hixl)：本仓已集成 Zread 代码仓智能体，旨在通过 AI 技术为您提供更深度的代码理解与技术支持，开启在线智能代码学习与知识问答体验！

> ⚠️ 说明：
当前代码AI Agent服务处于试点阶段。在使用过程中，如果您发现 AI 生成的内容存在准确性问题，或对智能助手的功能有任何改进建议，欢迎通过 Issues 与我们交流，您的反馈对我们非常重要！


## 📝相关信息

- [贡献指南](CONTRIBUTING.md)
- [安全声明](SECURITY.md)
- [许可证](LICENSE)
- [所属SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)

## 🤝联系我们

本项目功能和文档正在持续更新和完善中，欢迎您关注最新版本。

- **问题反馈**：通过GitCode[【Issues】](https://gitcode.com/cann/hixl/issues)提交问题。
- **社区互动**：通过GitCode[【讨论】](https://gitcode.com/cann/hixl/discussions)参与交流。
- **经验分享**：通过GitCode[【Wiki】](https://gitcode.com/cann/hixl/wiki)分享经验总结。
- **加入交流群**：通过扫描下方微信二维码添加HIXL小助手微信，加入微信群与我们进一步交流。

<img src="docs/figures/HIXL_contact.png" alt="contact us" width="50%">
