# LLM-DataDist Development

<!-- md-trans-meta sourceCommit=f2bcda9be0f3efe299188dcad27c86a94106da2c translatedAt=2026-11-19T01:01:42.856Z pushedAt=2026-11-19T01:04:51.818Z nl=2 -->

## Quick Start

### Overall Development Process

**Overview**

For the following descriptions, inference frameworks are abstracted into these modules:

- Resource initialization module.
- KV Cache management module, including memory creation, allocation (PA scenarios), and destruction.
- Model inference module.
- Resource release module.

This section describes how to enable LLM-DataDist in an inference framework.

**Development Process**

1. Locate the framework's resource initialization module and call the LLM-DataDist initialization API during this phase.
2. Locate its KV Cache management module and register user-allocated memory with LLM-DataDist through its registration API.
3. The inference framework must separate the Prefill and Decode phases, deploy their inference scripts on different cluster nodes, and connect P/D as needed.
4. Before Decode, receive Prefill output as input and use LLM-DataDist KV Cache transfer APIs to pull or push the KV Cache stored on Prefill.
5. Run the Prefill and Decode inference scripts separately.
6. Release LLM-DataDist resources in the framework's resource release module.

### Preparing the Environment

The following products are supported:

<!-- npu="910b" id1 -->
- Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. When the Server uses HCCS, LLM-DataDist APIs support only D2D.
<!-- end id1 -->

<!-- npu="A3" id2 -->
- Atlas A3 products: With HCCS, LLM-DataDist APIs do not support Host memory as a remote Cache.
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR&950DT products use UB within a supernode and RoCE between supernodes.
<!-- end id3 -->

Install the driver, firmware, and CANN following [CANN Software Installation](https://www.hiascend.com/document/detail/en/CANNCommunityEdition/latest/softwareinst/instg/instg_0000.html?OS=openEuler&InstallType=netyum).

In heterogeneous scenarios, the receiver and sender must use the same MTU (maximum transmission unit) configuration. For details on querying and configuring the MTU, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference).

Use `hccn_tool` to query Device IP addresses and test inter-device connectivity. Devices across clusters must have RDMA connections to enable LLM-DataDist. For details, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference). Common commands are listed below.

|Command|Scenarios|
|--|--|
|hccn_tool [-i %d] -link -g|Queries the specified Device network port Link status. `-i` specifies the Device. Example: `hccn_tool -i 0 -link -g`.|
|hccn_tool [-i %d] -ip -g|Queries the IP address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -g`.|
|hccn_tool [-i %d] -ip -inet6 -g|Queries the IPv6 address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -inet6 -g`.|
|hccn_tool [-i %d] -ping -g [address %s ]|Queries ping results from a Device to a destination. `-i` specifies a Device on the current server, and `address` specifies the destination. Example: `hccn_tool -i 0 -ping -g address 192.168.2.1`.|

The following environment variables are also used. See [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/hccl_env/README.md).

|Name|Scenarios|
|--|--|
|HCCL_RDMA_TC, HCCL_RDMA_SL|If the customer has planned the parameter-plane network and specified traffic types and priorities for various services, use these two variables to set the traffic class and priority of parameter-plane collective communication to match that plan.|
|HCCL_RDMA_RETRY_CNT, HCCL_RDMA_TIMEOUT|These specify the RDMA NIC retry count and retransmission timeout coefficient, respectively. Excessive values make the system insensitive to network anomalies and unable to detect network faults. If the values are too small, NIC hardware may fail to mask transient network interruptions, directly disrupting the application. Choose appropriate values based on your network conditions. For example, configure these values based on the duration range of most transient interruptions. To reduce the impact of network jitter, we recommend configuring HCCL_RDMA_TIMEOUT using the following formula: HCCL_RDMA_TIMEOUT=log2(pull KV timeout * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096), rounded up. When both the pull KV timeout and HCCL_RDMA_RETRY_CNT have their default values, a value of 15 is recommended for HCCL_RDMA_TIMEOUT.|
|HCCL_INTRA_ROCE_ENABLE|Controls whether devices within a Server communicate over RoCE loopback.|

### Complete Example

This example uses LLM-DataDist APIs for KV Cache management in disaggregated deployments, offering integration and adaptation guidance for different frameworks.

Quick trial: Visit [GitCode](https://gitcode.com/cann/hixl), select the matching version, and obtain examples from `examples/cpp`.

## Features

### Connection Management

#### Establishing Connections

**Feature Overview**

Call `LinkLlmClusters` to establish communication connections between PD nodes for KV Cache transfers.

**Scenarios**

Connections are required for inter-node transfers. The TCP-like process uses Server listening information supplied at initialization and Client-initiated connection establishment. Server/Client identity is independent of prompt/decoder roles and can be set as needed.

Establish connections to add nodes when adjusting the cluster's PD ratio based on workload.

**Example**

Initialize LLM-DataDist. Set the listening Host IP and port on the Server.

The following address examples assume deployment on the same machine, with the Server listening at `127.0.0.1:3000`. For deployment across machines, replace the local and remote IP addresses with their respective reachable Host IP addresses, and ensure that the remote port matches the Server listening port.

```cpp
// Server side
LlmDataDist llm_data_dist(PROMPT_CLUSTER_ID, LlmRole::kPrompt);
std::map<AscendString, AscendString> options;
options[OPTION_DEVICE_ID] = "0";
//Replace with the actual IP and port
options[OPTION_LISTEN_IP_INFO] = "127.0.0.1:3000";
auto ret = llm_data_dist.Initialize(options);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
}
// Client side
LlmDataDist llm_data_dist(DECODER_CLUSTER_ID, LlmRole::kDecoder);
std::map<AscendString, AscendString> options;
options[OPTION_DEVICE_ID] = "0";
auto ret = llm_data_dist.Initialize(options);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] Initialize failed, ret = %u\n", ret);
    return -1;
}
```

Call `LinkLlmClusters` on the Client to establish connections.

```cpp
std::vector<Status> rets;
std::vector<ClusterInfo> clusters;
ClusterInfo cluster_info;
IpInfo local_ip_info{};
// Replace with the actual local IP
local_ip_info.ip = "127.0.0.1";
IpInfo remote_ip_info{};
// Replace with the actual peer IP
remote_ip_info.ip = "127.0.0.1";
// Replace with the listening port configured during Server initialization
remote_ip_info.port = 3000;
cluster_info.remote_cluster_id = PROMPT_CLUSTER_ID;
cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info));
cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info));
clusters.emplace_back(std::move(cluster_info));
auto ret = llm_data_dist.LinkLlmClusters(clusters, rets);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] LinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] LinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

**Exception Handling**

If `LinkLlmClusters` fails, check connectivity between the machines, the Host IP address, and whether the port is occupied.

#### Disconnecting

**Feature Overview**

Call UnlinkLlmClusters to disconnect and clean up communication connections between Prompt and Decode.

**Scenarios**

Disconnect to clean up abnormal connections after a P or D cluster node fails, or to close existing connections when adjusting the cluster's PD node ratio.

**Example**

Disconnection supports two methods:

Initiate disconnection on the Client, typically for connections without faults.

```cpp
// Use the same clusters as for connection establishment
auto ret = llm_data_dist.UnlinkLlmClusters(clusters, rets);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnlinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] UnlinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

Force disconnection on both P and D sides, typically for faulty connections.

```cpp
// Both P and D sides
std::vector<Status> rets;
std::vector<ClusterInfo> clusters;
ClusterInfo cluster_info;
IpInfo local_ip_info;
// Replace with the actual local IP
local_ip_info.ip = "127.0.0.1";
IpInfo remote_ip_info;
// Replace with the actual peer IP
remote_ip_info.ip = "127.0.0.1";
// Replace with the listening port configured during Server initialization
remote_ip_info.port = 3000;
cluster_info.remote_cluster_id = PROMPT_CLUSTER_ID;
cluster_info.local_ip_infos.emplace_back(std::move(local_ip_info)); // local_ip_infos uses the local Host IP address
cluster_info.remote_ip_infos.emplace_back(std::move(remote_ip_info)); // remote_ip_infos uses the peer Host IP address
clusters.emplace_back(std::move(cluster_info));
auto ret = llm_data_dist.UnlinkLlmClusters(clusters, rets, 1000, true);
if (ret != LLM_SUCCESS) {
    printf("[ERROR] UnlinkLlmClusters failed, ret = %u\n", ret);
    return -1;
}
for (const auto &inner_ret : rets) {
    if (inner_ret != LLM_SUCCESS) {
        printf("[ERROR] UnlinkLlmClusters failed, ret = %u\n", inner_ret);
        return -1;
    }
}
```

**Exception Handling**

If UnlinkLlmClusters fails due to a network failure, use forced disconnection instead.

### KV Cache Management

**Feature Overview**

Before LLM-DataDist connection establishment, allocate memory and register it through `RegisterKvCache`. After disconnection, unregister through `UnregisterKvCache`.

The main KV Cache management APIs and their functions are as follows:

|API|Function|
|--|--|
|RegisterKvCache|Registers a local Cache.|
|UnregisterKvCache|Unregisters a local Cache.|
|PullKvCache|Pulls a remote Cache into a local Cache, regardless of role.|
|PullKvBlocks|Pulls a remote Blocks Cache into a local Cache using block lists, regardless of role.|
|PushKvCache|Pushes a local Cache to a remote node, regardless of role.|
|PushKvBlocks|Pushes a local Blocks Cache to a remote node using block lists, regardless of role.|

**Scenarios**

Primarily used for KV Cache transfers and movement between distributed clusters.

**Example (Ordinary Cache Transfers)**

This example describes how to use APIs for KV Cache registration, unregistration, and transfer in a general Cache transfer scenario, with pseudocode for each application role.

1. Initialize LLM-DataDist on P and D and allocate and register memory following [Establishing Connections](llm-datadist_develop.md#establishing-connections).

    ```cpp
    void OnError(LlmDataDist &llm_data_dist, int64_t cache_id)
    {
        if (cache_id > 0) {
            (void) llm_data_dist.UnregisterKvCache(cache_id);
        }
        llm_data_dist.Finalize();
    }
    CacheDesc kv_cache_desc{};
    kv_cache_desc.num_tensors = NUM_TENSORS;
    kv_cache_desc.data_type = DT_INT32;
    kv_cache_desc.shape = {8, 16};
    std::vector<uint64_t> addrs;  // Fill in as needed
    int64_t cache_id = -1;
    auto ret = llm_data_dist.RegisterKvCache(kv_cache_desc, addrs, {}, cache_id);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. Connect LLM-DataDist on P and D following [Establishing Connections](llm-datadist_develop.md#establishing-connections).

3. Transfer KV Cache memory on P/D as needed. Release associated resources on failure. There are two transfer methods.

    - Call `PullKvCache` to pull KV Cache.

    ```cpp
    // Run full inference on P, write the cache, and notify D that it can pull
    // Pull the cache on D
    CacheIndex cache_index{PROMPT_CLUSTER_ID, 1, 0};
    ret = llm_data_dist.PullKvCache(cache_index, cache, 0);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PullKvCache failed, ret = %u\n", ret);
        return -1;
    }
    // Run incremental inference
    ```

    - Call `PushKvCache` to push KV Cache.

    ```cpp
    // After a layer completes on P, push it immediately from the transfer thread to overlap most transfer time with computation
    CacheIndex dst_cache_index{DECODE_CLUSTER_ID, 1, 0};
    KvCacheExtParam ext_param{};
    ext_param.src_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    ext_param.dst_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    // Tensor count per layer; adjust for the model
    ext_param.tensor_num_per_layer = 2;
    ret = llm_data_dist.PushKvCache(cache, dst_cache_index, 0, -1, ext_param);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PushKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

4. On exit, disconnect the P and D sides following the example in [Disconnecting](llm-datadist_develop.md#disconnecting), call `UnregisterKvCache` to unregister, then `finalize` to release resources.

**Example (Blocks Cache Transfers)**

This example covers KV Cache registration and transfer with Blocks Cache (block-based cache management), with pseudocode for each application role.

1. Initialize and connect LLM-DataDist on P and D following [Establishing Connections](llm-datadist_develop.md#establishing-connections).
2. For each model layer on P and D, allocate KV Cache for the calculated `num_block` count and register it with LLM-DataDist. The upper-layer framework reuses this cache across requests and frees the memory when the application finishes.

    ```cpp
    void OnError(LlmDataDist &llm_data_dist, int64_t cache_id)
    {
        if (cache_id > 0) {
            (void) llm_data_dist.UnregisterKvCache(cache_id);
        }
        llm_data_dist.Finalize();
    }
    CacheDesc kv_cache_desc{};
    kv_cache_desc.num_tensors = NUM_TENSORS;
    kv_cache_desc.data_type = DT_INT32;
    kv_cache_desc.shape = {8, 16};
    std::vector<uint64_t> addrs;  // Fill in as needed
    int64_t cache_id = -1;
    auto ret = llm_data_dist.RegisterKvCache(kv_cache_desc, addrs, {}, cache_id);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] RegisterKvCache failed, ret = %u\n", ret);
        return -1;
    }
    ```

3. For each new request on P, the inference framework assigns `block_index` values. After inference, the request's KV Cache is stored at those block indices. Send the model output and request `block_table` to the D-side model as input.
4. For each new request on D, assign `block_index` values and call `PullKvBlocks` to transfer KV Cache to the target locations using the mapping between P and D block indices.

    - Call `PullKvBlocks` on D to pull KV Cache.

    ```cpp
    // Run full inference on P, write the cache, and notify D that it can pull
    // Pull the cache on D
    CacheIndex cache_index{PROMPT_CLUSTER_ID, 1, 0};
    std::vector<uint64_t> prompt_blocks = {0, 1, 2, 3};
    std::vector<uint64_t> decoder_blocks = {3, 2, 1, 0};
    auto ret = llm_data_dist.PullKvBlocks(cache_index, cache, prompt_blocks, decoder_blocks);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PullKvBlocks failed, ret = %u\n", ret);
        return -1;
    }
    // Run incremental inference
    ```

    - Call `PushKvBlocks` on P to push KV Cache.

    ```cpp
    // After a layer completes on P, push it immediately from the transfer thread to overlap most transfer time with computation
    CacheIndex dst_cache_index{DECODE_CLUSTER_ID, 1};
    KvCacheExtParam ext_param{};
    ext_param.src_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    ext_param.dst_layer_range =  std::pair<int32_t, int32_t>(0, 0);
    // Tensor count per layer; adjust for the model
    ext_param.tensor_num_per_layer = 2;
    std::vector<uint64_t> prompt_blocks = {0, 1, 2, 3};
    std::vector<uint64_t> decoder_blocks = {3, 2, 1, 0};
    ret = llm_data_dist.PushKvBlocks(cache, dst_cache_index, prompt_blocks, decoder_blocks, ext_param);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] PushKvBlocks failed, ret = %u\n", ret);
        return -1;
    }
    ```

5. On exit, disconnect the P and D sides following the example in [Disconnecting](llm-datadist_develop.md#disconnecting), call `UnregisterKvCache`, and release resources.

**Exception Handling**

- `LLM_KV_CACHE_NOT_EXIST` means the peer KV Cache does not exist. Check whether the peer process is abnormal or the corresponding request has completed inference. Other requests are unaffected; retry after checking the process.
- `LLM_WAIT_PROCESS_TIMEOUT` or `LLM_TIMEOUT` means a KV pull timed out, indicating a connection problem. Disconnect and try establishing the connection again.
- `LLM_NOT_YET_LINK` means no connection to the remote cluster.

## Advanced Topics

### Switching Roles

**Scenarios**

This is primarily used when the number of PD cluster nodes is fixed, allowing nodes to switch roles with one another as workloads change and make full use of resources.

**Related APIs**

Call `SetRole` to switch the LLM-DataDist role and use options to switch Client/Server mode.

**Example**

This example switches Decoder to Prompt and Client to Server. After switching, call other LLM-DataDist APIs based on application functionality.

1. Initialize LLM-DataDist.

    ```cpp
    LlmDataDist llm_data_dist(DECODER_CLUSTER_ID, LlmRole::kDecoder);
    std::map<AscendString, AscendString> options;
    options[OPTION_DEVICE_ID] = "0";
    auto ret = llm_data_dist.Initialize(options);
    if (ret != LLM_SUCCESS) {
        printf("[ERROR] Initialize failed, ret = %u\n", ret);
        return -1;
    }
    ```

2. Call `SetRole` to switch roles. Specify a listening Host IP and port when switching Client to Server; omit them when switching Server to Client.

    ```cpp
    std::map<AscendString, AscendString> options;
    //Replace with the actual IP and port
    options[OPTION_LISTEN_IP_INFO] = "127.0.0.1:3000";
    llm_data_dist.SetRole(LlmRole::kPrompt, options);
    ```

**Exception Handling**

For error handling, see LLM-DataDist Error Codes.
