<div align="center">

# HIXL

<h4>Simple, reliable, and efficient point-to-point data transmission for clusters</h4>

[![Documentation](https://img.shields.io/badge/docs-HIXL-Bookshelf-brightgreen)](https://gitcode.com/cann/hixl/blob/master/docs/README.md)
[![SIG](https://img.shields.io/badge/SIG-hccl-yellow)](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)
[![Meeting](https://img.shields.io/badge/meeting-Meetings-yellow)](https://meeting.osinfra.cn/cann?sig=sig-hccl)
[![Contributing](https://img.shields.io/badge/CONTRIBUTING-Contribution-teal)](https://gitcode.com/cann/hixl/blob/master/CONTRIBUTING.md)
[![WeChat](https://img.shields.io/badge/contact_us-WeChat-blue)](docs/zh/figures/HIXL_contact.png)
[![zread](https://img.shields.io/badge/Ask_Zread-_.svg?style=flat&color=00b0aa&labelColor=000000&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB3aWR0aD0iMTYiIGhlaWdodD0iMTYiIHZpZXdCb3g9IjAgMCAxNiAxNiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTQuOTYxNTYgMS42MDAxSDIuMjQxNTZDMS44ODgxIDEuNjAwMSAxLjYwMTU2IDEuODg2NjQgMS42MDE1NiAyLjI0MDFWNC45NjAxQzEuNjAxNTYgNS4zMTM1NiAxLjg4ODEgNS42MDAxIDIuMjQxNTYgNS42MDAxSDQuOTYxNTZDNS4zMTUwMiA1LjYwMDEgNS42MDE1NiA1LjMxMzU2IDUuNjAxNTYgNC45NjAxVjIuMjQwMUM1LjYwMTU2IDEuODg2NjQgNS4zMTUwMiAxLjYwMDEgNC45NjE1NiAxLjYwMDFaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00Ljk2MTU2IDEwLjM5OTlIMi4yNDE1NkMxLjg4ODEgMTAuMzk5OSAxLjYwMTU2IDEwLjY4NjQgMS42MDE1NiAxMS4wMzk5VjEzLjc1OTlDMS42MDE1NiAxNC4xMTM0IDEuODg4MSAxNC4zOTk5IDIuMjQxNTYgMTQuMzk5OUg0Ljk2MTU2QzUuMzE1MDIgMTQuMzk5OSA1LjYwMTU2IDE0LjExMzQgNS42MDE1NiAxMy43NTk5VjExLjAzOTlDNS42MDE1NiAxMC42ODY0IDUuMzE1MDIgMTAuMzk5OSA0Ljk2MTU2IDEwLjM5OTlaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik0xMy43NTg0IDEuNjAwMUgxMS4wMzg0QzEwLjY4NSAxLjYwMDEgMTAuMzk4NCAxLjg4NjY0IDEwLjM5ODQgMi4yNDAxVjQuOTYwMUMxMC4zOTg0IDUuMzEzNTYgMTAuNjg1IDUuNjAwMSAxMS4wMzg0IDUuNjAwMUgxMy43NTg0QzE0LjExMTkgNS42MDAxIDE0LjM5ODQgNS4zMTM1NiAxNC4zOTg0IDQuOTYwMVYyLjI0MDFDMTQuMzk4NCAxLjg4NjY0IDE0LjExMTkgMS42MDAxIDEzLjc1ODQgMS42MDAxWiIgZmlsbD0iI2ZmZiIvPgo8cGF0aCBkPSJNNCAxMkwxMiA0TDQgMTJaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00IDEyTDEyIDQiIHN0cm9rZT0iI2ZmZiIgc3Ryb2tlLXdpZHRoPSIxLjUiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIvPgo8L3N2Zz4K&logoColor=ffffff)](https://zread.ai/hicann/hixl)
</div>

## 🔥 What's New

- Apr 2026: HIXL supported device UBoE. For details, see [Request](https://gitcode.com/cann/hixl/pull/365). HIXL intergenerational capability was enhanced, supporting heterogeneous compute of A2, A3, and A5 ([Issue #138](https://gitcode.com/cann/hixl/issues/138) and [Issue #115](https://gitcode.com/cann/hcomm/issues/115)).
- Mar 2026: HIXL supported FabricMem for SuperPoDs. For details, see [FabricMem](./docs/en/FabricMem.md).
- Jan 2026: LLM-DataDist and HIXL supported Host RoCE transmission for next-generation chipsets.
- Dec 2025: Implemented D2rH direct transmission based on the A3 scale-up plane. Added support for link pools and IPv6, and interconnected with the next-generation TENT architecture of the Mooncake community.
- Nov 2025: HIXL allowed for asynchronous transmission, supporting higher concurrency in non-blocking data transmission.
- Oct 2025: The HIXL project was open-sourced. It provides efficient unilateral communication capabilities such as multi-link D2D, D2H, and H2D on Ascend processors, and optimizes the performance in scenarios with small data volume and batch transmission. HIXL has been integrated with multiple open source communities, including [Mooncake](https://github.com/kvcache-ai/Mooncake/issues/719) and [DeepLink](https://github.com/DeepLink-org/DLSlime/pull/21).


## 🚀 Overview
Huawei Xfer Library (HIXL) is a flexible and efficient Ascend unilateral communication library. It provides simple, reliable, and efficient point-to-point data transfers for clusters, and opens up to users through simple APIs, building a bridge between multi-AI applications and multi-link data transfers. It can be used to build a wide range of use cases, such as prefill-decode disaggregation in foundation models, parameter switching after reinforcement learning, and model parameter caching.

**Advantages**
- **One-sided zero-copy communication**: HIXL provides simple and reliable one-sided communication APIs. Once local memory data is ready, direct data transmission to remote memory can be completed through one-sided operations without any action required from the remote node. This provides core technical support for building scheduling mechanisms that overlap communication and computation. Meanwhile, zero-copy enables direct data transmission between user memory spaces, avoiding redundant data movement, reducing memory bandwidth usage, and lowering memory capacity consumption.
- **Hardware difference shielding and multi-link high-speed interconnect across devices**: HIXL masks underlying hardware differences across Ascend processors, so users do not need to adapt code for different chip architectures. At the communication link layer, this technology supports multiple high-speed interconnection protocols, such as RDMA and HCCS. The communication bandwidth can reach up to 119 GB/s, enabling seamless high-speed interconnection between devices with different architectures (such as A2 and A3 Ascend processors), meeting the requirements for low latency and high throughput.
- **Minimalist API design and deep adaptation to open-source frameworks**: Featuring a minimalist API design with only about 10 core calls, HIXL lowers the integration barrier for developers and provides complete C++ and Python language bindings. It has been deeply integrated with open-source frameworks such as Mooncake and DeepLink. Mainstream inference engines including vLLM and SGLang can directly call its APIs to implement efficient cross-device KV Cache transmission, reducing memory access latency by 20% during LLM inference and improving inference throughput.

<img src="./docs/en/figures/architecture.png" alt="Architecture">


**Core Components**

- **HIXL Engine**: As the core transmission engine, it provides basic transmission APIs and supports multiple memory transmission types such as D2D, D2H, and H2D. It is compatible with various transmission protocols including HCCS and RDMA, enabling high-speed and reliable data transmission. It natively supports multiple data link types and is suitable for complex scenarios in homogeneous and heterogeneous clusters. It can swiftly adapt to link changes and schedule resources under dynamic cluster scaling, forming a reliable communication foundation for overall cluster operation.
- **LLM-DataDist**: Built on the HIXL Engine, it provides a set of data transmission APIs with KV cache semantics, enabling fast and flexible integration with inference engines such as vLLM and SGLang.


**Performance**

When 128 MB data is transmitted on the Ascend A3 processor:
- The bandwidth of the HIXL transmission engine can reach 119 GB/s when the data is transmitted over the HCCS link.
- The bandwidth of the HIXL transmission engine can reach 22 GB/s when the data is transmitted over the RDMA link.

<img src="./docs/en/figures/perf.png" alt="Performance data">

For more details, see [Benchmarks](benchmarks/README.md).


## 🔍 Directory Structure

The key directories of this project are as follows:

```
└── build.sh                       # Project build script
├── benchmarks                     # Benchmark performance cases of the project
├── cmake                          # Project build directory
├── CMakeLists.txt                 # CMakeLists of the project
├── docs                           # Project documentation
│  ├── cpp                         # C++ documentation
│  └── python                      # Python documentation
├── examples                       # End-to-end sample development and calling examples
│  ├── cpp                         # C++ samples
│  ├── python                      # Python samples
├── include                        # Header files
│  ├── hixl
│  ├── cs
│  ├── adxl
│  └── llm_datadist
├── README.md
├── scripts                        # Script path
│  └── package
├── src                            # Source code path
│  ├── hixl
│  ├── llm_datadist
│  ├── ops
│  └── python
└── tests                          # Test project directory
```


## ⚡️ Quick Start

If you want to have a quick hands-on experience on the build and sample execution of this component, visit the following documents to get started.

- [Build](./docs/en/build.md): describes how to build and install the component, including how to use tests to perform local verification after the successful build.
- [Sample execution](examples/README.md): describes how to run the sample code in an end-to-end manner, including C++ and Python samples.


## 📖 Tutorials

If you want to learn more about the APIs of the component and modify the source code, visit the following documents:

- [C/C++ APIs]: describes the C++ APIs.
- [Python APIs]: describes Python APIs.

For more information about the HIXL development guide and technical articles, see the following documents:

- [HIXL Bookshelf](docs/README.md): provides links to HIXL-related documents, technical articles, and training videos.

## 🤖 AI Agent Support

- [AI-assisted programming](.agents/README.md): introduces some default skills and their triggers in the repository, which improve developers' programming efficiency and code quality.
- [Zread chatbot](https://zread.ai/hicann/hixl): This repository has integrated Zread, the code repository agent, aiming to deepen your code understanding and offer technical support through AI-powered knowledge Q&A.

> ⚠️ Note
The AI Agent is still in pilot. If you find any inaccuracies in the AI-generated content or have any suggestions for improving the functions of our agent, please feel free to submit an issue. We truly value your feedback.


## 📝 Related Information

- [Contributing Guide](CONTRIBUTING.md)
- [Security Statement](SECURITY.md)
- [License](./LICENSE)
- [Owning SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)

## 🤝 Contact Us

Features and documentation are updated regularly. Please follow the latest version for the most up-to-date information.

- **Issues**: Submit queries or report bugs via GitCode [Issues](https://gitcode.com/cann/hixl/issues).
- **Interaction**: Participate in [discussion](https://gitcode.com/cann/hixl/discussions) on GitCode.
- **Sharing**: Share your experience on GitCode [Wiki](https://gitcode.com/cann/hixl/wiki).
- **User group**: Scan the WeChat QR code below to add the HIXL assistant on WeChat and join the WeChat group to communicate with the community members and us.

<img src="./docs/en/figures/HIXL_contact.png" alt="contact us" width="50%">
