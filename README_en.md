<div align="center">

# HIXL

<h4>Simple, reliable, and efficient point-to-point data transmission for cluster scenarios</h4>

[![Documentation](https://img.shields.io/badge/docs-HIXL_Bookshelf-brightgreen)](https://gitcode.com/cann/hixl/blob/master/docs/README.md)
[![SIG](https://img.shields.io/badge/SIG-hccl-yellow)](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)
[![meeting](https://img.shields.io/badge/meeting-Community_Meeting-yellow)](https://meeting.osinfra.cn/cann?sig=sig-hccl)
[![contributing](https://img.shields.io/badge/CONTRIBUTING-Contribution_Guide-teal)](https://gitcode.com/cann/hixl/blob/master/CONTRIBUTING.md)
[![WeChat](https://img.shields.io/badge/contact_us-HIXL_WeChat_Group-blue)](docs/en/figures/HIXL_contact.png)
[![zread](https://img.shields.io/badge/Ask_Zread-_.svg?style=flat&color=00b0aa&labelColor=000000&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB3aWR0aD0iMTYiIGhlaWdodD0iMTYiIHZpZXdCb3g9IjAgMCAxNiAxNiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTQuOTYxNTYgMS42MDAxSDIuMjQxNTZDMS44ODgxIDEuNjAwMSAxLjYwMTU2IDEuODg2NjQgMS42MDE1NiAyLjI0MDFWNC45NjAxQzEuNjAxNTYgNS4zMTM1NiAxLjg4ODEgNS42MDAxIDIuMjQxNTYgNS42MDAxSDQuOTYxNTZDNS4zMTUwMiA1LjYwMDEgNS42MDE1NiA1LjMxMzU2IDUuNjAxNTYgNC45NjAxVjIuMjQwMUM1LjYwMTU2IDEuODg2NjQgNS4zMTUwMiAxLjYwMDEgNC45NjE1NiAxLjYwMDFaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00Ljk2MTU2IDEwLjM5OTlIMi4yNDE1NkMxLjg4ODEgMTAuMzk5OSAxLjYwMTU2IDEwLjY4NjQgMS42MDE1NiAxMS4wMzk5VjEzLjc1OTlDMS42MDE1NiAxNC4xMTM0IDEuODg4MSAxNC4zOTk5IDIuMjQxNTYgMTQuMzk5OUg0Ljk2MTU2QzUuMzE1MDIgMTQuMzk5OSA1LjYwMTU2IDE0LjExMzQgNS42MDE1NiAxMy43NTk5VjExLjAzOTlDNS42MDE1NiAxMC42ODY0IDUuMzE1MDIgMTAuMzk5OSA0Ljk2MTU2IDEwLjM5OTlaIiBmaWxsPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik0xMy43NTg0IDEuNjAwMUgxMS4wMzg0QzEwLjY4NSAxLjYwMDEgMTAuMzk4NCAxLjg4NjY0IDEwLjM5ODQgMi4yNDAxVjQuOTYwMUMxMC4zOTg0IDUuMzEzNTYgMTAuNjg1IDUuNjAwMSAxMS4wMzg0IDUuNjAwMUgxMy43NTg0QzE0LjExMTkgNS42MDAxIDE0LjM5ODQgNS4zMTM1NiAxNC4zOTg0IDQuOTYwMVYyLjI0MDFDMTQuMzk4NCAxLjg4NjY0IDE0LjExMTkgMS42MDAxIDEzLjc1ODQgMS42MDAxWiIgZmlsbPSIjZmZmIi8%2BCjxwYXRoIGQ9Ik00IDEyTDEyIDRMNCAxMiIgZmlsbD0iI2ZmZiI%2BCjxwYXRoIGQ9Ik00IDEyTDEyIDQiIHN0cm9rZT0iI2ZmZiIgc3Ryb2tlLXdpZHRoPSIxLjUiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIvPgo8L3N2Zz4K&logoColor=ffffff)](https://zread.ai/hicann/hixl)
</div>

## 🔥 Latest News

- [2026/05] [HIXL Application Development Series Tutorials](https://gitcode.com/cann/cann-learning-hub/tree/master/tutorials/hixl_development) released, covering core concepts and application development methods of Ascend one-sided communication library.
- [2026/04] HIXL supports Device UBoE, see [Issue #275](https://gitcode.com/cann/hixl/issues/275); HIXL intergenerational capability enhanced, supporting A2/A3/A5 heterogeneous compute [Issue #138](https://gitcode.com/cann/hixl/issues/138), [Issue #115](https://gitcode.com/cann/hcomm/issues/115).
- [2026/03] HIXL supports FabricMem mode within SuperPods, see [FabricMem](docs/en/FabricMem.md).
- [2026/01] LLM-DataDist and HIXL support Host RoCE transmission capability for next-generation chips.
- [2025/12] D2rH direct transmission implemented based on A3 scale-up plane; added link pool and IPv6 support, and integrated with Mooncake community's next-generation TENT architecture.
- [2025/11] HIXL provides asynchronous transmission capability, supporting higher concurrency in non-blocking data transmission scenarios.
- [2025/10] HIXL project open-sourced, providing efficient multi-link D2D/D2H/H2D one-sided communication capabilities on Ascend chips, and optimizing performance for small data batch transmission scenarios. HIXL has integrated with multiple open-source communities including [Mooncake](https://github.com/kvcache-ai/Mooncake/issues/719), [DeepLink](https://github.com/DeepLink-org/DLSlime/pull/21), etc.

## 🚀 Overview

HIXL (Huawei Xfer Library) is a flexible and efficient Ascend one-sided communication library, providing simple, reliable, and efficient point-to-point data transmission capabilities for cluster scenarios through easy-to-use APIs. It bridges multiple AI applications and multiple transmission links. It can be used to build various business scenarios such as LLM PD disaggregation, RL post-training parameter switching, model parameter caching, etc.

**Core Advantages**

- **One-sided Zero-Copy Communication Mechanism**: HIXL provides simple and reliable one-sided communication interfaces. After local memory data is ready, direct data transmission to remote memory can be completed through one-sided operations. This mechanism requires no operation from the remote node, providing core technical support for building scheduling mechanisms that overlap communication and computation. Meanwhile, zero-copy capability enables direct data transmission between user memory spaces, avoiding redundant data movement, reducing memory bandwidth usage, and decreasing memory capacity consumption.
- **Hardware Difference Shielding, Multi-link Cross-device High-speed Interconnection**: HIXL shields underlying hardware differences of Ascend series chips, users don't need to adapt code for different chip architectures. At the communication link layer, this technology natively supports multiple high-speed interconnection protocols including RDMA, HCCS, etc. Communication bandwidth can reach up to 119GB/s, enabling seamless high-speed interconnection across architecture devices (such as A2 series and A3 series Ascend chips), meeting low-latency, high-throughput requirements.
- **Minimalist API Design, Deep Adaptation to Open-source Frameworks**: HIXL adopts minimalist API interface design, with only about 10 core calls, lowering developer integration barrier, while providing complete C++/Python language interface support. Currently has deep integration with Mooncake, DeepLink and other open-source frameworks. Mainstream inference engines like vLLM, SGLang can also directly call HIXL API to complete efficient cross-device KV Cache transmission, reducing memory access latency by 20% during LLM inference, significantly improving inference throughput.

<img src="docs/en/figures/architecture.png" alt="Architecture">

**Core Components**

- **HIXL Engine**: As the core transmission engine, provides basic transmission interfaces, supports multiple memory type transmissions such as D2D, D2H, H2D. Compatible with multiple transmission protocols including HCCS, RDMA, etc. Enables high-speed, reliable data transmission. Natively supports multiple data link types, can flexibly handle complex scenarios in homogeneous and heterogeneous clusters. Facing dynamic cluster node scaling requirements, can quickly complete link adaptation and resource scheduling, building reliable communication foundation for overall cluster operation.
- **LLM-DataDist**: Built on HIXL Engine, provides a set of data transmission interfaces with KV Cache semantics. Can quickly and flexibly integrate with inference engines like vLLM, SGLang.

**Performance**

In 128M data transmission scenario on Ascend A3 chip:

- Through HCCS link transmission, HIXL transmission engine bandwidth can reach 119GB/s
- Through RDMA link transmission, HIXL transmission engine bandwidth can reach 22GB/s

<img src="docs/en/figures/perf.png" alt="Performance Data">

See [Benchmarks](benchmarks/README.md) for more details.

## 🔍 Directory Structure

Key directories of this project are described as follows:

```sh
├── build.sh                       # Project build script
├── benchmarks                     # Project benchmark performance cases
├── cmake                          # Project build directory
├── CMakeLists.txt                 # Project CMakeList
├── docs                           # Project documentation
│  ├── zh                          # Chinese documentation
│  └── en                          # English documentation
│  └── README.md                   # Chinese documentation guide
│  └── README_en.md                # English documentation guide
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

If you want to quickly experience the component build and sample execution, please visit the following documents for simple tutorials.

- [Build](docs/en/build.md): Introduces component compilation and installation, including local verification using tests after successful compilation.
- [Sample Execution](examples/README.md): Introduces how to execute sample code end-to-end, including C++ and Python samples.

## 📖 Learning Tutorials

If you want to deeply understand the component interfaces and modify source code, please visit the following documents for detailed tutorials.

- [C/C++ APIs](docs/zh/api/cpp/README.md): C++ interface introduction.
- [Python APIs](docs/zh/api/python/README.md): Python interface introduction.

If you want to deeply understand HIXL development guide and technical articles, you can refer to the following documents:

- [HIXL Documentation Overview](docs/README.md): Summarizes HIXL related materials, technical articles and training video links.

## 🤖 AI Agent Support

- [AI-assisted Programming](.agents/README.md): Introduces some default skills and trigger methods used in the repository, for improving developer programming efficiency and code quality.
- [Zread Intelligent Q&A](https://zread.ai/hicann/hixl): This repository has integrated Zread code repository agent, aiming to provide deeper code understanding and technical support through AI technology, enabling online intelligent code learning and knowledge Q&A experience!

> ⚠️ Note:
The current code AI Agent service is in pilot stage. During use, if you find accuracy issues in AI-generated content, or have any improvement suggestions for the assistant's functionality, welcome to communicate with us through Issues. Your feedback is very important to us!

## 📝 Related Information

- [Contributing Guide](CONTRIBUTING.md)
- [Security Statement](SECURITY.md)
- [License](LICENSE)
- [Owning SIG](https://gitcode.com/cann/community/tree/master/CANN/sigs/hccl)

## 🤝 Contact Us

This project's functionality and documentation are continuously being updated and improved. Welcome to follow the latest version.

- **Issue Feedback**: Submit issues through GitCode [Issues](https://gitcode.com/cann/hixl/issues).
- **Community Interaction**: Participate in discussions through GitCode [Discussions](https://gitcode.com/cann/hixl/discussions).
- **Experience Sharing**: Share experience through GitCode [Wiki](https://gitcode.com/cann/hixl/wiki).
- **Join Discussion Group**: Scan the WeChat QR code below to add HIXL assistant WeChat, join WeChat group for further communication with us.

<img src="docs/en/figures/HIXL_contact.png" alt="contact us" width="50%">
