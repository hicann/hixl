# LLM-DataDist APIs

<!-- md-trans-meta sourceCommit=53e9e702fa1264d5a079c2b9f93d0655b9d4c290 translatedAt=2026-11-19T01:01:42.801Z pushedAt=2026-11-19T01:04:51.491Z nl=2 -->

## Applicable Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 inference products/Atlas A2 training products: Supported
<!-- end id3 -->

<!-- npu="910b" id16 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id16 -->

## LlmDataDist Constructor

**Description**

Creates an LLM-DataDist object.

**Prototype**

```cpp
LlmDataDist(uint64_t cluster_id, LlmRole role)
```

 **Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| cluster_id | Input | Cluster ID identifying **LlmDataDist**. Must be unique among all instances participating in connection establishment. |
| role | Input | Type: **LlmRole**. This parameter only identifies the current role and does not affect the transfer process. |

**Return Value**

None

**Exception Handling**

None

**Constraints**

None

## \~LlmDataDist\(\)

**Description**

Destructor of an LLM-DataDist object.

**Prototype**

```cpp
~LlmDataDist()
```

**Parameters**

None

**Return Value**

None

**Exception Handling**

None

**Constraints**

None

## Initialize

**Description**

Initializes LLM-DataDist.

**Prototype**

```cpp
Status Initialize(const std::map<AscendString, AscendString> &options)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| options | Input | Initialization options. For details, see Table 1. |

**Table 1** options

| Parameter | Optional/Required | Description |
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | Optional | Configuring this option makes LLM-DataDist a Server; omitting it makes it a Client.<br>For a Server, specify a Host IP address and port.<br>Example: **"192.168.1.1:26000"**. Multiple IP addresses and ports are unsupported. |
| OPTION_DEVICE_ID | Required | Device ID of the current process, such as **"0"**. A single process with multiple devices is unsupported. |
| OPTION_SYNC_CACHE_WAIT_TIME | Optional | Timeout for KV operations, in ms. Default: 1000ms. Applies to:<br><br>  - PullKvCache<br>  - PullKvBlocks<br>  - PushKvCache<br>  - PushKvBlocks |
| OPTION_LOCAL_COMM_RES | Optional | Local communication resource information as a JSON string. |
| OPTION_TRANSFER_BACKEND | Optional | Transfer backend engine used by LLM-DataDist. Currently supports **"hixl"**. Usage:<br><br>- Set **OPTION_LISTEN_IP_INFO** during initialization: with the hixl backend, each transfer endpoint can act as both client and server.<br><br>- Call **LinkLlmClusters** to establish a connection before transferring with a peer. |
| OPTION_GLOBAL_RESOURCE_CONFIG | Optional | Configures global resources as a JSON string. `tls_config.*` configures LLM-DataDist control plane TLS independently of OPTION_TRANSFER_BACKEND. Other fields are passed through to HIXL only when OPTION_TRANSFER_BACKEND is "hixl". For fields and constraints, see [Control Plane TLS Configuration in OPTION_GLOBAL_RESOURCE_CONFIG](#llm_tls_config) and [Initialize in the HIXL API Reference](./HIXL-interface.md#initialize). Omitting this option retains default behavior.<br>Example: '{"comm_resource_config.listen_port": 26666, "comm_resource_config.max_active_channels": 128}'.<br>Invalid JSON, incorrect field types, or invalid values cause initialization to fail. |

Additional notes on **OPTION_LOCAL_COMM_RES**:

  <!-- npu="A3,910b" id5 -->
- Omitted or empty: Resources are generated automatically, and connections are established using collective communication communicators with a limit of 512 links per card. Note: If **OPTION_BUFFER_POOL** (or **adxl.BufferPool**) is **"0:0"** (staging pool disabled) and hcomm/toolkit is 9.1.0 or later, HixlCS is used without a connection limit. This method applies to the following models:
  <!-- npu="910b" id6 -->
  - Atlas A2 products
  <!-- end id6 -->
  <!-- npu="A3" id7 -->
  - Atlas A3 products
  <!-- end id7 -->
- Ranktable format with **version** **"1.0"** or **"1.2"**: Uses collective communication communicators to establish links, limited to 512 connections per card. Configure only Device information used by the current llm datadist; **server_count** and **rank_id** are unnecessary. This method applies to the following models:
  <!-- npu="910b" id8 -->
  - Atlas A2 products
  <!-- end id8 -->
  <!-- npu="A3" id9 -->
  - Atlas A3 products
  <!-- end id9 -->
  <!-- end id5 -->
- **version "1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later): Uses HixlCS without a connection limit. For the format, see [Communication Resource Configuration Fields](./HIXL-interface.md#communication-resource-configuration-fields).
 <!-- npu="950" id4 -->
 - For Ascend 950PR&950DT products, see [Communication Resource Configuration Fields](./HIXL-interface.md#communication-resource-configuration-fields) for the format. Also set **OPTION_TRANSFER_BACKEND** to **hixl**. **OPTION_LOCAL_COMM_RES** is required; an empty value does not trigger automatic generation.
 <!-- end id4 -->

For ranktable details in the table above, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/README.md).<br>For **OPTION_LOCAL_COMM_RES** version **"1.3"**, see [HIXL API Reference](./HIXL-interface.md#communication-resource-configuration-fields) for communication resource fields.

**Control Plane TLS Configuration in OPTION_GLOBAL_RESOURCE_CONFIG**

<a id="llm_tls_config"></a>

Configure the `tls_config.*` fields in `OPTION_GLOBAL_RESOURCE_CONFIG` to enable TCP TLS on the LLM-DataDist control plane. Control plane TLS protects only the control plane connections that LLM-DataDist establishes through HIXL, HIXL CS, or ADXL. It does not change how the KV Cache data plane transfers or encrypts data.

| Field | Data Type | Required/Optional | Description |
| --- | --- | --- | --- |
| tls_config.cert_file | String | Required when TLS is enabled | Path to the control plane TLS certificate file. It must be configured together with `tls_config.key_file`. |
| tls_config.key_file | String | Required when TLS is enabled | Path to the control plane TLS private key file. It must be configured together with `tls_config.cert_file`. |
| tls_config.ca_file | String | Optional | Path to the CA file used to verify the peer certificate. Required when `tls_config.verify_peer` is true. |
| tls_config.verify_peer | Boolean | Optional | Whether to verify the peer certificate. Defaults to false. When false, an encrypted connection is established without verifying the peer identity. In production environments, we recommend setting it to true, in which case the peer must provide a valid certificate. The control plane establishes connections by IP address and does not perform hostname or SNI verification. |
| tls_config.ciphers | String | Optional | OpenSSL cipher suites; applies only to TLS 1.2. For example, `ECDHE-RSA-AES256-GCM-SHA384`. |
| tls_config.enable_cert_refresh | Boolean | Optional | Whether to automatically load changed certificate files. Defaults to false. When enabled, a new certificate applies only to connections established afterward; existing connections are unaffected. If certificate loading fails, the original certificate remains in use. |

The following constraints apply:

- Control plane TLS is disabled if no `tls_config.*` fields are configured.
- All LLM-DataDist, HIXL, HIXL CS, and ADXL instances in the same process must use the same control plane TLS configuration, including whether TLS is enabled. Inconsistent configurations cause initialization or object creation to fail. These requirements also apply when any of these components are used together.
- To change the control plane TLS configuration, first destroy all LLM-DataDist, HIXL, HIXL CS, and ADXL objects in the process that use this configuration, and then reinitialize them.
- `tls_config.cert_file` and `tls_config.key_file` must be configured together. When `tls_config.verify_peer` is true, `tls_config.ca_file` is required. An incorrect field type, a file loading failure, or a certificate/private key mismatch causes initialization to fail; it does not fall back to a plaintext connection.
- Control plane TLS increases handshake time. When control plane TLS is enabled, we recommend setting the `LinkLlmClusters` timeout to at least 2000ms.

Example:

```cpp
LlmDataDist llm_data_dist(0U, LlmRole::kDecoder);
std::map<AscendString, AscendString> options;
options[OPTION_DEVICE_ID] = "0";
options[OPTION_GLOBAL_RESOURCE_CONFIG] = R"({
    "tls_config.cert_file": "/path/to/cert.pem",
    "tls_config.key_file": "/path/to/key.pem",
    "tls_config.ca_file": "/path/to/ca.pem",
    "tls_config.verify_peer": true,
    "tls_config.enable_cert_refresh": true
})";
auto ret = llm_data_dist.Initialize(options);
if (ret != LLM_SUCCESS) {
  printf("[ERROR] Initialize failed, ret = %u\n", ret);
}
```

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

Use this API together with **Finalize**. After successful initialization, call **Finalize** before any exit to release resources. Otherwise, resources may be released in an unexpected order, causing errors.

## Finalize

**Description**

Releases LLM-DataDist resources.

**Prototype**

```cpp
void Finalize()
```

**Parameters**

None

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

None

**Exception Handling**

None

**Constraints**

- Use this API together with **Initialize**.
- When a Decode cluster pulls KV from a Prompt cluster, the Prompt cluster must wait until the Decode cluster finishes synchronizing KV data before calling this API. Otherwise, the operation fails.
- When a Prompt cluster synchronizes KV data to a Decode cluster, the Decode cluster must wait until synchronization finishes before calling this API. Otherwise, the operation fails.
- This API cannot be called concurrently with other APIs.

## SetRole

**Description**

Sets the LLM-DataDist role.

**Prototype**

```cpp
Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options = {})
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| role | Input | Role type. Type: **LlmRole**. |
| options | Input | Parameters for setting the role. For currently supported parameters, see Table 1 below. |

**Table 1** Settings

| Setting | Optional/Required | Description |
| --- | --- | --- |
| OPTION_LISTEN_IP_INFO | Optional | - If initialized as a Client, configure this option with a listening Host IP and port to switch to Server, for example, **"192.168.1.1:26000"**. Otherwise, omit it.<br>  - If initialized as a Server, omitting this option switches to Client. Setting it retains Server mode; a port different from the initialization port replaces it. Example: **"192.168.1.1:26001"**.<br>  - When using the hixl backend, **SetRole** cannot change the listening port.|

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Role set successfully.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- **LLM\_EXIST\_LINK**: Remaining connection resources exist.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

Disconnect the current DataDist's connections before use.

## LinkLlmClusters

**Description**

Establishes connections between LlmDataDist instances. Called on the Client.

**Prototype**

```cpp
Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| clusters | Input | Clusters to connect, of type **ClusterInfo**. Configure **remote_cluster_id** and **remote_ip_infos**. If **Initialize(LlmDataDist)** does not specify **OPTION_LOCAL_COMM_RES**, also configure **local_ip_infos**. Each **ClusterInfo** supports only one **ip_info** in each of **local_ip_infos** and **remote_ip_infos**. |
| rets | Output | Connection result for each cluster. |
| timeout_in_millis | Input | Connection timeout, in ms. Default: 1000ms. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Returned only if all clusters connect successfully.
- Other values: Connection failed. Check **rets** for each cluster's connection establishment result.

**Exception Handling**

- **LLM\_ALREADY\_LINK**: The current cluster is already connected to the remote cluster.
- **LLM\_LINK\_FAILED**: Connection establishment failed.

**Constraints**

- Call **Initialize** on both Client and Server before calling this API.
  <!-- npu="A3,910b" id10 -->
- When **OPTION_LOCAL_COMM_RES** is empty or **version** is **"1.0"** or **"1.2"**, connections use collective communication communicators, with a maximum of 512 communications. Excessive connections may cause memory OOM and degrade KV Cache transfer performance. Note: If **OPTION_LOCAL_COMM_RES** is empty, **OPTION_BUFFER_POOL** (or **adxl.BufferPool**) is **"0:0"** (staging pool disabled), and hcomm/toolkit is 9.1.0 or later, HixlCS is used without a connection limit. This constraint applies to the following models:
  <!-- npu="910b" id11 -->
  - Atlas A2 products
  <!-- end id11 -->
  <!-- npu="A3" id12 -->
  - Atlas A3 products
  <!-- end id12 -->
  <!-- end id10 -->
- With **OPTION_LOCAL_COMM_RES** version **"1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later), connections use HixlCS without a connection limit.
- We recommend setting the timeout to at least 200ms, or at least 2000ms if Device TLS or control plane TLS is enabled. Use the following command to query Device TLS status. Control plane TLS status depends on whether `tls_config.*` is configured:

  ```sh
  hccn_tool [-i %d] -tls -g [host]
  ```

- Register all memory on both Client and Server before this API. Memory registered after connection establishment does not support remote access.
  <!-- npu="A3,910b" id13 -->
- In containers, if **OPTION\_LOCAL\_COMM\_RES** is omitted or empty, map **/etc/hccn.conf** into the container or ensure that **hccn_tool** exists under **/usr/local/Ascend/driver/tools**. Otherwise, add its directory to **PATH**. In the example below, **hccn_tool_install_path** is that directory. This constraint applies to the following models:
  <!-- npu="910b" id14 -->
  - Atlas A2 products
  <!-- end id14 -->
  <!-- npu="A3" id15 -->
  - Atlas A3 products
  <!-- end id15 -->

  ```sh
  export PATH=$PATH:${hccn_tool_install_path}
  ```
  <!-- end id13 -->

<!-- npu="A3,910b" id20 -->
- For Device RoCE, use consistent Device RoCE address configurations throughout the communication cluster. Mixing IPv6-only nodes with IPv4/IPv6 dual-stack nodes is unsupported. This constraint applies to the following products:
  <!-- npu="910b" id21 -->
  - Atlas A2 products
  <!-- end id21 -->
  <!-- npu="A3" id22 -->
  - Atlas A3 products
  <!-- end id22 -->
<!-- end id20 -->

## UnlinkLlmClusters

**Description**

Disconnects LlmDataDist instances.

**Prototype**

```cpp
Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000, bool force_flag = false)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| clusters | Input | Clusters to disconnect. Type: **ClusterInfo**. |
| rets | Output | Disconnection result for each cluster. |
| timeout_in_millis | Input | Disconnection timeout, in ms. |
| force_flag | Input | Whether to force disconnection. Default: No.<br>Forced disconnection removes only local connections, so both ends must call it.<br>Normal disconnection starts on the Client and removes both ends if no fault exists. With a connection fault, forced disconnection must also be called on the Server and may take longer. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **SUCCESS**: Returned only if all clusters disconnect successfully.
- Other values: Disconnection failed. Check **rets** for each cluster's disconnection result.

**Exception Handling**

**LLM\_UNLINK\_FAILED**: Disconnection failed.

**Constraints**

Call **Initialize** before calling this API.

## PullKvCache

**Description**

Pulls a Cache from a remote node into a local Cache.

**Prototype**

```cpp
Status PullKvCache(const CacheIndex &src_cache_index,
                   const Cache &dst_cache,
                   uint32_t batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {})
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| src_cache_index | Input | Remote source Cache index. |
| dst_cache | Input | Local destination Cache. Specify only the **cache_id** returned by **RegisterKvCache**. |
| batch_index | Input | Local destination batch index. |
| size | Input | An integer greater than 0 specifies the size to pull in bytes.<br>**-1** pulls the full data.<br>Default: **-1**. |
| ext_param | Input | In **ext_param**, the difference between **second** and **first** in **src_layer_range** must equal that in **dst_layer_range**. Both endpoints default to **-1**, meaning all layers. Valid range: [0, maximum available layer index], with **first <= second**. The maximum available layer index is:<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>**tensor_num_per_layer** ranges from 1 to the current cache's total tensor count, default 2. If either layer range is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault **tensor_num_per_layer** value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- **LLM\_NOT\_YET\_LINK**: Not connected to the remote cluster.
- **LLM\_TIMEOUT**: Pull timed out.
- **LLM\_KV\_CACHE\_NOT\_EXIST**: Local or remote KV Cache does not exist.
- Other values: Failure.

**Constraints**

Call **LinkLlmClusters** to complete initialization before this API.

## PullKvBlocks

**Description**

Pulls a Cache from a remote node into a local Cache using block lists.

**Prototype**

```cpp
Status PullKvBlocks(const CacheIndex &src_cache_index,
                    const Cache &dst_cache,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {})
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| src_cache_index | Input | Remote source Cache index. |
| dst_cache | Input | Local destination Cache. Specify only the **cache_id** returned by **RegisterKvCache**. |
| src_blocks | Input | Remote source Cache block index list. |
| dst_blocks | Input | Local destination Cache block index list. |
| ext_param | Input | In **ext_param**, the difference between **second** and **first** in **src_layer_range** must equal that in **dst_layer_range**. Both endpoints default to **-1**, meaning all layers. Valid range: [0, maximum available layer index], with **first <= second**. The maximum available layer index is:<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>**tensor_num_per_layer** ranges from 1 to the current cache's total tensor count, default 2. If either layer range is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault **tensor_num_per_layer** value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- **LLM\_NOT\_YET\_LINK**: Not connected to the remote cluster.
- **LLM\_TIMEOUT**: Pull timed out.
- **LLM\_KV\_CACHE\_NOT\_EXIST**: Remote KV Cache does not exist.
- Other values: Failure.

**Constraints**

Call **LinkLlmClusters** to complete initialization before this API.

## PushKvCache

**Description**

Pushes a Cache to a remote node.

**Prototype**

```cpp
Status PushKvCache(const Cache &src_cache,
                   const CacheIndex &dst_cache_index,
                   uint32_t src_batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {});
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| src_cache | Input | Local source Cache. Specify only the **cache_id** returned by **RegisterKvCache**. |
| dst_cache_index | Input | Remote destination Cache index. |
| src_batch_index | Input | Local source batch index. |
| size | Input | Currently supports only the default value **-1**. |
| ext_param | Input | In **ext_param**, the difference between **second** and **first** in **src_layer_range** must equal that in **dst_layer_range**. Both endpoints default to **-1**, meaning all layers. Valid range: [0, maximum available layer index], with **first <= second**. The maximum available layer index is:<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>**tensor_num_per_layer** ranges from 1 to the current cache's total tensor count, default 2. If either layer range is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault **tensor_num_per_layer** value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- **LLM\_NOT\_YET\_LINK**: Not connected to the remote cluster.
- **LLM\_TIMEOUT**: Push timed out.
- **LLM\_KV\_CACHE\_NOT\_EXIST**: Local or remote KV Cache does not exist.
- Other values: Failure.

**Constraints**

Call **LinkLlmClusters** to complete initialization before this API.

## PushKvBlocks

**Description**

Pushes a Cache to a remote node using block lists.

**Prototype**

```cpp
Status PushKvBlocks(const Cache &src_cache,
                    const CacheIndex &dst_cache_index,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {});
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| src_cache | Input | Local source Cache. Specify only the **cache_id** returned by **RegisterKvCache**. |
| dst_cache_index | Input | Remote destination Cache index. |
| src_blocks | Input | Source Cache block index list. |
| dst_blocks | Input | Destination Cache block index list. |
| ext_param | Input | In **ext_param**, the difference between **second** and **first** in **src_layer_range** must equal that in **dst_layer_range**. Both endpoints default to **-1**, meaning all layers. Valid range: [0, maximum available layer index], with **first <= second**. The maximum available layer index is:<br>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1<br>**tensor_num_per_layer** ranges from 1 to the current cache's total tensor count, default 2. If either layer range is nondefault, **tensor_num_per_layer** may retain its default or use another value. A nondefault **tensor_num_per_layer** value must divide the cache's total tensor count evenly. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- **LLM\_PARAM\_INVALID**: Invalid parameter.
- **LLM\_NOT\_YET\_LINK**: Not connected to the remote cluster.
- **LLM\_TIMEOUT**: Push timed out.
- **LLM\_KV\_CACHE\_NOT\_EXIST**: Local or remote KV Cache does not exist.
- Other values: Failure.

**Constraints**

Call **LinkLlmClusters** to complete initialization before this API.

## RegisterKvCache

**Description**

Registers local KV Cache memory.

**Prototype**

```cpp
Status RegisterKvCache(const CacheDesc &cache_desc,
                       const std::vector<uint64_t> &addrs,
                       const RegisterCfg &cfg,
                       int64_t &cache_id);
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| cache_desc | Input | Local Cache description. |
| addrs | Input | Local Cache addresses. No more than 240 addresses. |
| cfg | Input | Reserved parameter. |
| cache_id | Output | Registered Cache ID, used to construct a **Cache** for subsequent KV transfer API calls. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- Other values: Failure.

**Constraints**

Call this API after **Initialize** completes.

<!-- npu="A3,910b" id17 -->
Up to 20GB of Host memory can be registered for HDK earlier than 25.5.0, and 1TB for HDK 25.5.0 or later. Registering more memory consumes more OS memory. This constraint applies to the following models:

<!-- npu="910b" id18 -->
- Atlas A2 products
<!-- end id18 -->
<!-- npu="A3" id19 -->
- Atlas A3 products
<!-- end id19 -->
<!-- end id17 -->

## UnregisterKvCache

**Description**

Unregisters local KV Cache memory.

**Prototype**

```cpp
Status UnregisterKvCache(int64_t cache_id);
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| cache_id | Input | ID generated by local Cache registration. Returns success by default if **cache_id** does not exist. |

**Example**

See [Running the Examples](../../../../examples/cpp/README.md).

**Return Value**

- **LLM\_SUCCESS**: Success.
- Other values: Failure.

**Constraints**

Call **Initialize** before this API. **cache\_id** must be returned by **RegisterKvCache**.
