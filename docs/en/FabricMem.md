# FabricMem Mode
## Background

With the exponential growth of Large Language Model (LLM) parameter scales, inference deployment faces unprecedented memory pressure. Taking GPT-4-level models as an example, hundreds of billions of parameters require hundreds of gigabytes of GPU memory just for weights under FP16 precision. However, the largest memory consumption comes from the KV Cache—in long-sequence inference scenarios, KV Cache capacity demands often exceed the model weights themselves several times over.

The industry response is to build a **multi-level caching architecture**, using GPU memory as the L1 cache, distributed DRAM as the L2 cache, and SSD/NVMe as the L3 cache. Distributed KV Cache systems such as Mooncake are becoming mainstream choices. HIXL, as a transport backend, has already integrated with Mooncake, making efficient cross-superpod KV Cache transfer a core challenge.

In DRAM-based distributed memory pools, traditional solutions rely on RoCE (RDMA over Converged Ethernet) networks, which deliver about 20 GB/s of saturated bandwidth. In Atlas 800T A3 SuperPoD deployments, this becomes a significant performance bottleneck. To address this, HIXL provides the FabricMem mode, increasing intra-SuperPoD transfer bandwidth to the order of 100 GB/s.

## Overall Solution

In an Atlas 800T A3 SuperPoD, the DRAM memory of all compute nodes is uniformly addressed, allowing NPUs to directly access the remote node memory via high-speed HCCS links.

The core benefits of FabricMem are as follows:

- **Unified DRAM addressing within a SuperPoD**: breaking node boundaries to pool memory resources
- **High-bandwidth D2RH/RH2D transfers**: high-speed bidirectional channels between device and remote host
- **CPU-bypass unilateral communication**: source-initiated transfers with zero overhead on the peer side

### VMM-based Memory Management

The FabricMem mode relies fundamentally on CANN's **Virtual Memory Manager** mechanism, enabling globally unified addressing and direct access by all processes. The implementation is as follows:

1. Each process allocates its own on-chip memory and DRAM memory: It first calls `aclrtMallocPhysical` to allocate physical memory, then calls `aclrtReserveMemAddress` to reserve virtual memory, and finally calls `aclrtMapMem` to map the physical memory to the virtual memory.
2. Exchanges physical addresses.
3. Maps the physical address to the access process's page table.
4. Initiates SDMA access to read and write any process's on-chip memory and DRAM memory.

![Diagram](https://raw.gitcode.com/user-images/assets/7838749/4c57e5b8-7005-4184-be6a-8012509da066/截屏2026-02-14_16.29.00.png "截屏2026-02-14 16.29.00.png")

Data flow for writing data from the local NPU on-chip memory to the remote host memory:

```mermaid
graph LR
    subgraph Machine1 [Machine1]
        direction TB
        DDR1[DDR] --- CPU1[CPU]
        CPU1 --- L1_1520_1[L1 switching plane]
        NPU1[NPU] --> L1_1520_1
        linkStyle 2 stroke:#ff0000,stroke-width:2px
    end

    L1_1520_1 --> L2-1520[L2 switching plane]
    linkStyle 3 stroke:#ff0000,stroke-width:2px

    L2-1520 --> L1_1520_2[L1 switching plane]
    linkStyle 4 stroke:#ff0000,stroke-width:3px

    subgraph Machine2 [Machine2]
        direction TB
        L1_1520_2 --> CPU2[CPU]
        linkStyle 5 stroke:#ff0000,stroke-width:2px
        CPU2 --> DDR2[DDR]
        linkStyle 6 stroke:#ff0000,stroke-width:2px
        L1_1520_2 --- NPU2[NPU]
    end

    style L2-1520 fill:#f9f,stroke:#333,stroke-width:2px
```

## Installation and Runtime Dependencies

| Dependency | Version Requirement |
| -------------------------------- |----------------------------------------------------------------------------------------------------------------------|
| HDK | [25.5 or later](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743/software) |
| LingQu Computing Network | [1.5.0 or later](https://support.huawei.com/enterprise/en/ascend-computing/lingqu-computing-network-pid-258003841/software) |
| CANN | **9.0 or later** |

Note: HDK 25.5 does not support the `aclrtMemRetainAllocationHandle` API. You must use ADXL-provided `MallocMem` and `FreeMem` APIs to manage host memory. HDK 26.0 or later allows direct calls to ACL APIs for host memory management.
- **Enabling method**: During engine initialization, configure `OPTION_ENABLE_USE_FABRIC_MEM` in `options`. The value `"1"` enables FabricMem while `"0"` disables it. See [HIXL Interface · options](../zh/api/cpp/HIXL-interface.md).
- **Optional global configuration**: Use `OPTION_GLOBAL_RESOURCE_CONFIG` to configure the capacity, start address, and per-task stream counts of the Fabric virtual memory pool. See the `fabric_memory.*` field in [HIXL Interface](../zh/api/cpp/HIXL-interface.md).

**Hardware scope**: Only **Atlas A3 training products** and **Atlas A3 inference products** are supported.

## Performance Reference

See [benchmarks/performance.md](../../benchmarks/performance.md)