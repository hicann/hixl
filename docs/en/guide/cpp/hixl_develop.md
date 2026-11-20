# HIXL Development

<!-- md-trans-meta sourceCommit=53e9e702fa1264d5a079c2b9f93d0655b9d4c290 translatedAt=2026-11-19T01:01:42.852Z pushedAt=2026-11-19T01:04:51.764Z nl=2 -->

## HIXL Overview

HIXL (Huawei Xfer Library), the Ascend one-sided communication library, provides high-performance, zero-copy point-to-point transfers for clusters through simple APIs.

In distributed memory pools, upper-layer frameworks such as Mooncake Store manage KV Cache but require remote memory reads and writes using local and remote addresses at the transfer layer. HIXL, the Ascend one-sided communication library, was developed for this scenario to provide transfers based solely on local and remote addresses.

**Table 1** HIXL and LLM-Datadist Comparison

|Aspect|HIXL|LLM-Datadist|
|--|--|--|
|Scenarios|Primarily used for memory transfers between distributed clusters.|Primarily used for KV Cache transfers and movement between distributed clusters.|
|Main Function|Connection management: connection establishment and disconnection. Memory transfer: registration, deregistration, and transfer. Staging transfers.|Connection management: connection establishment and disconnection. KV Cache transfer: registration, unregistration, and transfer.|

## Quick Start

### Overall Development Process

**Overview**

For the following descriptions, inference frameworks are abstracted into these modules:

- Resource initialization module.
- Memory management module, including creation, allocation, and destruction.
- Model inference module.
- Resource release module.

This section describes how to enable HIXL in an inference framework.

**Development Process**

1. Locate the framework's resource initialization module and call the HIXL initialization API during this phase.
2. Locate the framework's memory management module and register user-allocated memory with HIXL through its registration API.
3. Separate the framework's Prefill and Decode scripts and deploy them on different cluster nodes. Before Decode, receive Prefill output as input and call the HIXL memory transfer API to pull remote memory or push local memory to the peer.
4. Run the Prefill and Decode inference scripts separately.
5. Release HIXL resources in the framework's resource release module.

### Preparing the Environment

Supported products:

<!-- npu="910b" id1 -->
- Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products.
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR&950DT products use UB within a supernode and RoCE between supernodes.
<!-- end id3 -->

Install the driver, firmware, and CANN following [CANN Software Installation](https://www.hiascend.com/document/detail/en/CANNCommunityEdition/latest/softwareinst/instg/instg_0000.html?OS=openEuler&InstallType=netyum).

Use `hccn_tool` to query Device IP addresses and check inter-device connectivity. Devices across clusters must have RDMA connections to enable HIXL. For details, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference).

In heterogeneous scenarios, the receiver and sender must use the same MTU (maximum transmission unit) configuration. For details on querying and configuring the MTU, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference).

The following environment variables are also used. For details, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/hccl_env/README.md).

|Name|Scenarios|
|--|--|
|HCCL_RDMA_TC, HCCL_RDMA_SL|If the customer has planned the parameter-plane network and specified traffic types and priorities for various services, use these two variables to set the traffic class and priority of parameter-plane collective communication to match that plan.|
|HCCL_RDMA_RETRY_CNT, HCCL_RDMA_TIMEOUT|These specify the RDMA NIC retry count and retransmission timeout coefficient, respectively. Excessive values make the system insensitive to network anomalies and unable to detect network faults. If the values are too small, NIC hardware may fail to mask transient network interruptions, directly disrupting the application. Choose appropriate values based on your network conditions. For example, configure these values based on the duration range of most transient interruptions. To reduce the impact of network jitter, we recommend configuring HCCL_RDMA_TIMEOUT using the following formula: HCCL_RDMA_TIMEOUT=log2(pull KV timeout * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096), rounded up. When both the pull KV timeout and HCCL_RDMA_RETRY_CNT have their default values, a value of 15 is recommended for HCCL_RDMA_TIMEOUT.|
|HCCL_INTRA_ROCE_ENABLE|Controls whether devices within a Server communicate over RoCE loopback. With RoCE enabled (`HCCL_INTRA_ROCE_ENABLE=1`), also set `HCCL_INTRA_PCIE_ENABLE=0`. |

### Complete Example

This example uses HIXL APIs for memory transfers in disaggregated deployments, offering integration and adaptation guidance for different frameworks.

Quick trial: Visit [GitCode](https://gitcode.com/cann/hixl), select the matching version, and obtain examples from `examples/cpp`.

## Features

### Connection Management

#### Establishing Connections

**Feature Overview**

Call `Connect` to establish a communication connection to the Server for data transfers. P/D nodes can act as Server or Client as needed.

**Scenarios**

Connections are required for inter-node data transfer. The API uses a TCP-like process: the Server provides listening information during initialization, and the Client initiates the connection.

**Example**

1. Initialize HIXL. Set the listening Host IP and port on the Server.

    ```cpp
    // Server side
    Hixl engine;
    std::map<AscendString, AscendString> options;
    AscendString local_engine = "ip:port"; // Replace with the actual IP and valid port; this uniquely identifies HIXL
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    // Client side
    Hixl engine;
    std::map<AscendString, AscendString> options;
    AscendString local_engine = "ip:port"; // Replace with the actual IP; this uniquely identifies Hixl. Omit the port or use a negative value to disable listening
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. Call `Connect` on the Client to establish a connection.

    ```cpp
    AscendString remote_engine = "ip:port"; // Replace with the local engine of the Server to connect to
    auto ret = engine.Connect(remote_engine);
    if (ret != SUCCESS) {
        printf("[ERROR] Connect failed, ret = %u\n", ret);
        return -1;
    }
    ```

**Exception Handling**

If connection establishment fails, check connectivity between the machines, the Host IP address, and whether the port is occupied.

For more error handling, see HIXL Error Codes.

#### Disconnecting

**Feature Overview**

Call `Disconnect` to disconnect and clean up the communication connection.

**Scenarios**

Disconnect to clean up abnormal connections after a P or D cluster node fails, or to close existing connections when adjusting the cluster's PD node ratio.

**Example**

Initiate disconnection on the Client.

```cpp
AscendString remote_engine = "ip:port"; // Replace with the local engine of the Server to disconnect from
auto ret = engine.Disconnect(remote_engine);
if (ret != SUCCESS) {
    printf("[ERROR] Disconnect failed, ret = %u\n", ret);
    return -1;
}
```

**Exception Handling**

For more error handling, see HIXL Error Codes.

### Memory Management

**Feature Overview**

The main memory management APIs and their functions are as follows:

|API|Function|
|--|--|
|RegisterMem|Registers memory. Both Client and Server must call it before connection establishment.|
|DeregisterMem|Deregisters memory. Both Client and Server must call it after disconnection.|
|TransferSync|Reads remote memory into local memory or pushes local memory to the corresponding remote address.|

**Scenarios**

Primarily used for memory transfers between distributed clusters.

**Example**

This example describes how to use the transfer APIs for memory registration, deregistration, and transfer. Pseudocode examples for each application role are provided below.

1. Initialize HIXL on both Client and Server following [Establishing Connections](hixl_develop.md#establishing-connections).
2. On Client and Server, allocate and register memory of the required size for each request before establishing connections. Release associated resources on failure.

    ```cpp
    void OnError(Hixl &engine, const std::vector<MemHandle> &handles)
    {
        for (auto handle : handles) {
            (void) engine.DeregisterMem(handle);
        }
        engine.Finalize();
    }

    MemDesc desc{};
    desc.addr = reinterpret_cast<uintptr_t>(addr);
    desc.len = len;
    MemHandle handle = nullptr;
    auto ret = engine.RegisterMem(desc, MEM_HOST, handle);
    if (ret != SUCCESS) {
        printf("[ERROR] RegisterMem failed, ret = %u\n", ret);
        return -1;
    }
    ```

3. Memory transfers support two methods

    For D2RH and RH2D transfers over HCCS on Atlas A3 products, enable `OPTION_ENABLE_USE_FABRIC_MEM` for optimal transfer performance.

    - On the Client, call `TransferSync` with `operation=READ` to read remote memory locally. Register local and remote addresses at their respective ends before connection establishment.

        ```cpp
        TransferOpDesc desc{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), size};
        auto ret = engine.TransferSync(remote_engine, READ, {desc});
        if (ret != SUCCESS) {
            printf("[ERROR] TransferSync read failed, ret = %u\n", ret);
            return -1;
        }
        ```

    - On the Client, call `TransferSync` with `operation=WRITE` to push local memory remotely. Register local and remote addresses at their respective ends before connection establishment.

        ```cpp
        TransferOpDesc desc{reinterpret_cast<uintptr_t>(local), reinterpret_cast<uintptr_t>(remote), size};
        auto ret = engine.TransferSync(remote_engine, WRITE, {desc});
        if (ret != SUCCESS) {
            printf("[ERROR] TransferSync write failed, ret = %u\n", ret);
            return -1;
        }
        ```

4. On application exit, disconnect both P and D sides following [Disconnecting](hixl_develop.md#disconnecting), then call `finalize` to release resources.

**Exception Handling**

- `TIMEOUT` indicates a transfer timeout and a connection problem. Disconnect, then try establishing the connection again.
- `NOT_CONNECTED` indicates no connection to the peer.

For more error handling, see HIXL Error Codes.

### Staging Transfers

**Scenarios**

Staging transfers use intermediate buffers. Use this feature when RDMA direct transfers such as Host To Host have memory registration size limits.

**Example**

This example performs Host To Host staging transfers.

1. Initialize and connect HIXL on Client and Server following [Establishing Connections](hixl_develop.md#establishing-connections), additionally configuring the `"BufferPool"` staging memory pool option during initialization.

    ```cpp
    Hixl engine;
    std::map<AscendString, AscendString> options;
    options["BufferPool"] = "4:8";
    AscendString local_engine = "ip:port"; // Replace with the actual IP and valid port; this uniquely identifies Hixl
    auto ret = engine.Initialize(local_engine, options);
    if (ret != SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. Transfer memory as in [Memory Management](hixl_develop.md#memory-management), except Host memory need not be registered through `RegisterMem`. Registered memory uses direct transfers; unregistered memory uses staging transfers.

**Exception Handling**

- `TIMEOUT` indicates a transfer timeout and a connection problem. Disconnect, then try establishing the connection again.
- `NOT_CONNECTED` indicates no connection to the peer.
