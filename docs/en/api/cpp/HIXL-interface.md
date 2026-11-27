# HIXL APIs

<!-- md-trans-meta sourceCommit=ba11b19969b962647241df46403c1560f6668ba1 translatedAt=2026-11-19T01:01:42.786Z pushedAt=2026-11-19T01:04:51.413Z nl=2 -->

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

<!-- npu="910b" id37 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id37 -->

## HIXL Constructor

**Description**

Creates a **HIXL** object.

**Prototype**

```cpp
Hixl()
```

**Parameters**

None

**Return Value**

None

**Exception Handling**

None

**Constraints**

None

## \~Hixl\(\)

**Description**

Destructor of a **HIXL** object.

**Prototype**

```cpp
~Hixl();
```

**Parameters**

None

**Return Value**

None

**Exception Handling**

None

## Initialize

**Description**

Initializes **HIXL**. Call this API before calling other APIs.

**Prototype**

```cpp
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| local_engine | Input | **HIXL** identifier, which must be unique among all engines participating in connection establishment. For IPv4, the format is **host_ip:host_port** or **host_ip**. For IPv6, it is **[host_ip]:host_port** or **[host_ip]**. A loopback IP address is not recommended because it may conflict when multiple HIXL instances interact.<br>If **host_port** is set and **host_port>0**, the current **HIXL** acts as a Server and listens on the configured port. If **host_port** is omitted or **host_port<=0**, it acts as a Client and does not listen. |
| options | Input | HIXL initialization parameter dictionary with string keys and values. For supported options and values, see [Options](#options-description). |

<a id="options-description"></a>

**Options**

`options` is a dictionary of string key-value pairs. Keys are listed in the Option Key column below, and all values are strings. The following subsections describe values and constraints. See Options Supported by Each Product below for product support.

**Table 1** Options

| Option Key | Constant | Value Format | Required/Optional | Default Value | Description |
| --- | --- | --- | --- | --- | --- |
| `"EnableUseFabricMem"` | `OPTION_ENABLE_USE_FABRIC_MEM` | `"0"`/`"1"` | Optional | Not configured (disabled) | Enables Fabric Mem mode. See [EnableUseFabricMem](#option_enable_use_fabric_mem). |
| `"BufferPool"` | `OPTION_BUFFER_POOL` | `"${BUFFER_NUM}:${BUFFER_SIZE}"`, in MB. | Optional | `"4:8"` | Staging memory pool configuration. See [BufferPool](#option_buffer_pool). |
| `"RdmaTrafficClass"` | `OPTION_RDMA_TRAFFIC_CLASS` | Integer in [0,255] and a multiple of 4. | Optional | `132` | RDMA NIC traffic class. See [RDMA Configuration](#option_rdma). |
| `"RdmaServiceLevel"` | `OPTION_RDMA_SERVICE_LEVEL` | Integer in [0,7]. | Optional | `4` | RDMA NIC service level. See [RDMA Configuration](#option_rdma). |
| `"AutoConnect"` | `OPTION_AUTO_CONNECT` | `"0"`/`"1"` | Optional | Not configured (disabled) | Automatic connection mode. See [AutoConnect](#option_auto_connect). |
| `"GlobalResourceConfig"` | `OPTION_GLOBAL_RESOURCE_CONFIG` | JSON string | Optional | Not configured | Global resource configuration. See [GlobalResourceConfig](#option_global_resource_config). |
| `"LocalCommRes"` | `OPTION_LOCAL_COMM_RES` | JSON string | Optional | Not configured | Local communication resources. See [LocalCommRes](#option_local_comm_res). |

**Options Supported by Each Product**

<!-- npu="910b" id56 -->
- Atlas A2 products: `BufferPool`, `RdmaTrafficClass`, `RdmaServiceLevel`, `AutoConnect`, `GlobalResourceConfig`, and `LocalCommRes`.
<!-- end id56 -->
<!-- npu="A3" id57 -->
- Atlas A3 products: `EnableUseFabricMem`, `BufferPool`, `RdmaTrafficClass`, `RdmaServiceLevel`, `AutoConnect`, `GlobalResourceConfig`, and `LocalCommRes`.
<!-- end id57 -->
<!-- npu="950" id58 -->
- Ascend 950PR&950DT products: `EnableUseFabricMem`, `RdmaTrafficClass`, `RdmaServiceLevel`, `AutoConnect`, `GlobalResourceConfig`, and `LocalCommRes`.
<!-- end id58 -->

> Note: `RdmaTrafficClass` and `RdmaServiceLevel` apply only to RoCE scenarios.For environment variables, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/hccl_env/README.md). For ranktable, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/README.md).

<a id="option_enable_use_fabric_mem"></a>

**EnableUseFabricMem (OPTION_ENABLE_USE_FABRIC_MEM)**

- Option key: `"EnableUseFabricMem"`.
- Values: `"0"` disables Fabric Mem mode; `"1"` enables it.
- Default: Not configured (disabled).
- Purpose: Enables Fabric Mem mode for D2RH and RH2D transfers over HCCS.
- Constraints:
  - In a cluster, all nodes must use the same value.
  - When configured together with `"BufferPool"`, `"BufferPool"` supports only `"0:0"` (staging memory pool disabled).
- Example:

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_ENABLE_USE_FABRIC_MEM] = "1";
```

<a id="option_buffer_pool"></a>

**BufferPool (OPTION_BUFFER_POOL)**

- Option key: `"BufferPool"`.
- Value: `"${BUFFER_NUM}:${BUFFER_SIZE}"`, in MB. Set to `"0:0"` to disable the staging memory pool.
- Default: `"4:8"`.
- Purpose: Configures the staging memory pool and enables staging transfers when staging buffers are required, for example, when RDMA Host memory registration is size-limited or when staging improves performance for multiple small memory blocks (such as 128K).
- Constraints:
  - When configured together with `"EnableUseFabricMem"`, this option supports only `"0:0"` (staging memory pool disabled).
  - Use the same value wherever this configuration is used.
  - Increasing `${BUFFER_NUM}` is recommended for concurrent transfers.
  <!-- npu="A3,910b" id51 -->
  - When set to `"0:0"` with hcomm/toolkit 9.1.0 or later, HIXL uses HixlCS to establish connections, with no limit on the number of connections by default. Use channel_pool.max_channel to limit connections initiated by the local endpoint as a client.
  - For Atlas A2 products, if this option is omitted, only D2D is supported when the Server uses the HCCS transport protocol.
  <!-- end id51 -->
- Example:

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_BUFFER_POOL] = "4:8";
```

<a id="option_rdma"></a>

**RDMA Configuration (OPTION_RDMA_TRAFFIC_CLASS / OPTION_RDMA_SERVICE_LEVEL)**

- Option keys: `"RdmaTrafficClass"` and `"RdmaServiceLevel"`.
- Values:
  - `"RdmaTrafficClass"`: Integer in [0,255] and a multiple of 4.
  - `"RdmaServiceLevel"`: Integer in [0,7].
- Defaults: `"RdmaTrafficClass"` defaults to `132`; `"RdmaServiceLevel"` defaults to `4`.
- Purpose: Configures the RDMA NIC traffic class and service level, functionally equivalent to the environment variables `HCCL_RDMA_TC` and `HCCL_RDMA_SL`, respectively. When both are configured, these options take precedence; when only one is configured, that setting applies.
- Constraint: Applies to RoCE scenarios.
- Example:

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_RDMA_TRAFFIC_CLASS] = "132";
options[hixl::OPTION_RDMA_SERVICE_LEVEL] = "4";
```

<a id="option_auto_connect"></a>

**AutoConnect (OPTION_AUTO_CONNECT)**

- Option key: `"AutoConnect"`.
- Values: `"0"` disables Auto Connect mode; `"1"` enables it.
- Default: Not configured (disabled).
- Purpose: Allows transfers without explicitly establishing connections.
- Constraint: Abnormal connections are automatically cleaned up after transfer errors or peer destruction. Peer destruction is detected through heartbeats, with a default interval of 10s.
- Example:

```cpp
std::map<hixl::AscendString, hixl::AscendString> options;
options[hixl::OPTION_AUTO_CONNECT] = "1";
```

<a id="option_global_resource_config"></a>

**GlobalResourceConfig (OPTION_GLOBAL_RESOURCE_CONFIG)**

- Option key: `"GlobalResourceConfig"`.
- Value: JSON string.
- Default: Not configured.
- Purpose: Configures HIXL global resources, including communication resources and connection establishment, transfer batches, Fabric Mem, an asynchronous connection/disconnection thread pool (**connect_pool**), a connection pool (**channel_pool**), and local communication resource files used with [LocalCommRes](#option_local_comm_res). All fields are listed below.

**Table: GlobalResourceConfig Fields**

| Field | Data Type | Required/Optional | Overview |
| --- | --- | --- | --- |
| comm_resource_config.protocol_desc | String or string array | Optional | Allowed communication protocols and communication device placements. See [protocol_desc](#grc_protocol_desc). |
| comm_resource_config.link_priority | String or single-element string array | Optional | D2D connection priority for UB CTP within the same instance. Values must be uppercase; defaults to `"PEER2NET"`. See [link_priority](#grc_link_priority). |
| comm_resource_config.listen_port | JSON number or digit-only string | Optional | Device NIC listening port. See [listen_port](#grc_listen_port). |
| comm_resource_config.qos | Number | Optional | Communication protocol QoS. See [qos](#grc_qos). |
| comm_resource_config.max_active_channels | Number | Optional | Number of concurrently active device transfer channels in CS scenarios. See [max_active_channels](#grc_max_active_channels). |
| roce_config.hixl_backend | Boolean | Optional | Controls whether `roce:host` uses the HIXL native verbs path. Default: false. See [hixl_backend](#grc_hixl_backend). |
| roce_config.retry_cnt | Number or decimal digit string | Optional | RDMA retransmission count limit, in [1, 7], default 7. Takes precedence over the `HCCL_RDMA_RETRY_CNT` environment variable. See [roce](#grc_roce). |
| roce_config.timeout | Number or decimal digit string | Optional | RDMA retransmission timeout exponent, in [5, 20], default 20. Takes precedence over the `HCCL_RDMA_TIMEOUT` environment variable. See [roce](#grc_roce). |
| comm_resource_config.multi_channel.num_workers | Number | Optional | Worker count for concurrent multichannel transfers. See [multi_channel](#grc_multi_channel). |
| comm_resource_config.multi_channel.split_batch_size | Number | Optional | Buffer splitting granularity for concurrent multichannel transfers. See [multi_channel](#grc_multi_channel). |
| comm_resource_config.nic_topo_path | String | Optional | Specifies the affinity between Host RoCE NICs and Devices. See [nic_topo_path](#grc_nic_topo_path). |
| transfer_config.max_transfer_count_per_batch | Number or decimal digit string | Optional | Maximum number of buffers in an internal transfer batch. See [max_transfer_count_per_batch](#grc_transfer_batch). |
| fabric_memory.max_capacity | Number or numeric string | Optional | Virtual memory pool size. See [fabric_memory](#grc_fabric_memory). |
| fabric_memory.start_address | Number or numeric string | Optional | Virtual memory pool starting address. See [fabric_memory](#grc_fabric_memory). |
| fabric_memory.task_stream_num | Number or numeric string | Optional | Number of streams used by a task. See [fabric_memory](#grc_fabric_memory). |
| fabric_memory.enable_aicpu_unfold | Boolean | Optional | Whether AI CPU unfolds tasks. See [fabric_memory](#grc_fabric_memory). |
| connect_pool.thread_num | Number or numeric string | Optional | Connection thread pool size. See [connect_pool](#grc_connect_pool). |
| connect_pool.task_queue_capacity | Number or numeric string | Optional | Connection task queue capacity. See [connect_pool](#grc_connect_pool). |
| channel_pool.max_channel | Number or numeric string | Optional | Maximum number of connections. See [channel_pool](#grc_channel_pool). |
| channel_pool.high_waterline | Number or numeric string | Optional | High watermark for connection reclamation, in communicator mode only. See [channel_pool](#grc_channel_pool). |
| channel_pool.low_waterline | Number or numeric string | Optional | Low watermark for connection reclamation, in communicator mode only. See [channel_pool](#grc_channel_pool). |
| tls_config.cert_file | String | Required when TLS is enabled | Path to the control plane TCP TLS certificate file. See [tls_config](#grc_tls_config). |
| tls_config.key_file | String | Required when TLS is enabled | Path to the control plane TCP TLS private key file. See [tls_config](#grc_tls_config). |
| tls_config.ca_file | String | Optional | Path to the CA file used to verify the peer certificate. See [tls_config](#grc_tls_config). |
| tls_config.verify_peer | Boolean | Optional | Whether to require and verify a peer certificate. See [tls_config](#grc_tls_config). |
| tls_config.ciphers | String | Optional | OpenSSL cipher suites; applies only to TLS 1.2. See [tls_config](#grc_tls_config). |
| tls_config.enable_cert_refresh | Boolean | Optional | Whether to automatically load changed certificate files. See [tls_config](#grc_tls_config). |
| local_comm_res_path | String | Optional | Local communication resource JSON file path, used with LocalCommRes. See [local_comm_res_path](#grc_local_comm_res_path). |
| topo_file_path | String | Optional | Hardware topology JSON file path used for automatic LocalCommRes generation. See [topo_file_path](#grc_topo_file_path). |

**Applicable Products**

<!-- npu="950" id70 -->
- `comm_resource_config.link_priority`: Supported only on Ascend 950PR&950DT products.
<!-- end id70 -->
<!-- npu="950" id69 -->
- `comm_resource_config.nic_topo_path`: Supported only on Ascend 950PR&950DT products.
<!-- end id69 -->
<!-- npu="950,A3" id64 -->
- `fabric_memory.*`: Supported only on Atlas A3 and Ascend 950PR&950DT products.
<!-- end id64 -->
<!-- npu="A3,910b" id65 -->
- `channel_pool.high_waterline` and `channel_pool.low_waterline`: Supported only on Atlas A2 and Atlas A3 products in communicator mode.
<!-- end id65 -->

**Details and Examples**

**Communication Resources and Connection Establishment (`comm_resource_config.*`)**

<a id="grc_protocol_desc"></a>

- **protocol_desc**: Allowed communication protocols and communication device placements. Use `ub_ctp` for UB CTP in pure URMA mode; use `${protocol}:${placement}` for other configurations.
  - Supported values: `"ub_ctp"`, `"ubmem"`, `"roce:device"`, `"hccs:device"`, `"ub_ctp:device"`, `"ub_ctp:host"`, `"uboe:device"`, `"ub_rtp:device"`, and `"roce:host"`.
  - Filtering: When configured, filters both explicitly configured and automatically generated `endpoint_list` entries in `LocalCommRes` by this scope. Explicit `LocalCommRes` entries are not additionally filtered if this field is omitted.
  <!-- npu="950" id59 -->
  - Notes for Ascend 950PR&950DT products:
    - If this field is omitted or `protocol_desc` is empty, HIXL attempts to generate the LocalCommRes resources required for `"ub_ctp:device"`, `"uboe:device"`, and `"ub_rtp:device"` without filtering the generated results.
    - If only `"ub_ctp:device"` is configured, Device UB resources are generated automatically, and Host memory is mapped to Device addresses through UBMEM for transfer over Device UB connections. Configuring both `"ub_ctp:device"` and `"ub_ctp:host"` generates Device+Host UB resources and uses a pure URMA path; configuring `"ub_ctp"` is equivalent to this combination. Configuring only `"ub_ctp:host"` retains only the Host UB CTP Endpoint.
    - When `"roce:host"` is explicitly configured, the hixl engine generates the resources required for Host RoCE. Configure `nic_topo_path` to specify the affinity. Under certain conditions, if `nic_topo_path` is not configured, automatic selection uses the DCMI detection results. For details, see [nic_topo_path](#grc_nic_topo_path).
    - When `"ubmem"` is explicitly configured, a Device ubmem endpoint is generated automatically.
    - Setting the environment variable `HCCL_INTRA_ROCE_ENABLE=1` restricts transfers to RoCE, regardless of the protocol_desc configuration.
  <!-- end id59 -->
  - When manually configuring `LocalCommRes`, `"ub_ctp"` requires both Device and Host UB CTP Endpoints.
  <!-- npu="950,A3" id63 -->
  - `ubmem` supports only transfers within a supernode and is supported only on Atlas A3 and Ascend 950PR&950DT products (currently limited to a single server). On Atlas A3 products, configure `["ubmem", "roce:device"]` to enable automatic RoCE selection for transfers across supernodes.
  <!-- end id63 -->

<a id="grc_link_priority"></a>

- **link_priority**: Configures the D2D connection establishment priority for `ub_ctp` within the same instance. Values must be uppercase: only `"PEER2NET"` (CLOS) and `"PEER2PEER"` (fullmesh direct connections) are supported. Case is not converted, and whitespace is not trimmed.
  - Accepts a string or a single-element string array, such as `"PEER2NET"` or `["PEER2NET"]`. An empty string, an empty array, a multi-element array, or an invalid value or type returns `PARAM_INVALID`.
  - Defaults to `"PEER2NET"` when omitted, preferring CLOS when both types can be matched. Set `"PEER2PEER"` to restore the previous preference for direct connections. If the preferred type cannot be matched, selection falls back automatically. The existing plane selection order is retained within each type.
  - This field also applies when Device UB CTP resources are generated by default and `comm_resource_config.protocol_desc` is omitted; no explicit protocol configuration is required.
  - Only one connection is still selected for each communication direction. This field affects only D2D connection selection for UB CTP within the same instance.
  - Default behavior change: When both CLOS and fullmesh connections can be matched, the default preference changes from fullmesh to CLOS.

```cpp
options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
    R"({"comm_resource_config.protocol_desc":["ub_ctp:device"],
        "comm_resource_config.link_priority":"PEER2PEER"})";
```

Example configuring only connection priority while retaining default protocol selection:

```cpp
options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] =
    R"({"comm_resource_config.link_priority":"PEER2PEER"})";
```

<a id="grc_listen_port"></a>

- **listen_port**: Device NIC listening port, in [1, 65535]. Usage is described below.

<!-- npu="910b" id44 -->
  - Atlas A2 products: If this option is omitted, port `16666` is used.
<!-- end id44 -->
<!-- npu="A3" id45 -->
  - Atlas A3 products: If this option is omitted, port `16666` is used.
<!-- end id45 -->
<!-- npu="950" id46 -->
  - Ascend 950PR&950DT products: If omitted, the underlying communication component selects an available port, and HIXL queries the actual listening port automatically.
<!-- end id46 -->

<a id="grc_qos"></a>

- **qos**: Communication protocol QoS, in [0, 7]. Defaults to 0 if omitted.

<a id="grc_max_active_channels"></a>

- **max_active_channels**: Number of concurrently active device transfer channels in CS scenarios, in [1, 8192]. Defaults to 128 if omitted. Each active channel consumes 2 Stream resources. Leave headroom based on the card's Stream resource limit and the Streams already created by your application. For limits by card type, see **aclrtCreateStream** in [Runtime APIs](https://gitcode.com/cann/runtime/blob/master/docs/en/api_ref/README.md). Values outside [1, 8192] cause **Initialize** to return an invalid parameter error.

<a id="grc_hixl_backend"></a>

- **roce_config.hixl_backend**: Controls whether the Host RoCE (`roce:host`) communication path uses the HIXL native RDMA verbs implementation.
  - Value: Boolean (`true` or `false`), defaulting to `false` when omitted. Passing a non-Boolean value causes initialization to return `PARAM_INVALID`.
  - Scope: Applies only to `roce:host` endpoints. When set to `false`, the existing HCOMM implementation is used; when set to `true`, HIXL directly calls RDMA verbs for connection establishment and one-sided transfers. Other protocols (such as `roce:device`, `hccs:device`, `ub_ctp`, and `ubmem`) are unaffected.
  - Connection constraint: Both communication peers (Server and Client) must use the same setting and both endpoints must be `roce:host`; otherwise, connection establishment or Client creation fails.

Example:

```json
{
    "comm_resource_config.protocol_desc": ["roce:host"],
    "roce_config.hixl_backend": true
}
```

<a id="grc_roce"></a>

- **roce_config.retry_cnt / roce_config.timeout**: RoCE RDMA retransmission settings.
  - `retry_cnt`: Range [1, 7], default 7. Configures the RDMA RC QP hardware retransmission count limit (`ibv_qp_attr.retry_cnt`). When explicitly configured, it takes precedence over the `HCCL_RDMA_RETRY_CNT` environment variable; when omitted, it falls back to `HCCL_RDMA_RETRY_CNT` (or 7 if unset).
  - `timeout`: Range [5, 20], default 20. Configures the RDMA RC QP ACK timeout exponent (`ibv_qp_attr.timeout`, where each ACK timeout is approximately $4.096\ \mu\text{s} \times 2^{\text{timeout}}$). When explicitly configured, it takes precedence over the `HCCL_RDMA_TIMEOUT` environment variable; when omitted, it falls back to `HCCL_RDMA_TIMEOUT` (or 20 if unset).

Example:

```json
{
    "comm_resource_config.protocol_desc": ["roce:host"],
    "roce_config.hixl_backend": true,
    "roce_config.retry_cnt": 7,
    "roce_config.timeout": 20
}
```

<a id="grc_multi_channel"></a>

- **multi_channel.num_workers / multi_channel.split_batch_size**: Concurrent multichannel transfer settings.
  - `num_workers`: Range [1, 16], default 1 (multichannel disabled). When this option is configured, N independent CS clients are created during connection establishment for UBOE, UB_RTP, Host RoCE, UB_CTP, HCCS, and Device RoCE. Both synchronous and asynchronous transfers support concurrent multichannel operation to improve small-packet bandwidth. More workers consume more threads, Streams, and other device resources. Using no more than 8 workers is recommended.
  - `split_batch_size`: Range [1, 4294967295], default 128. Multichannel splitting is enabled when a transfer's buffer count exceeds split_batch_size. The actual worker count depends on num_workers and the buffer count and does not exceed num_workers.

Example:

```sh
{
    "comm_resource_config.protocol_desc": ["ub_ctp:device", "ub_ctp:host"], //Optional. Allowed communication protocols and communication device placements.
    "comm_resource_config.listen_port": "26666", //Optional. Device NIC listening port, in [1, 65535].
    "comm_resource_config.qos": 0, //Optional. Communication protocol QoS, in [0, 7]. Default: 0.
    "comm_resource_config.max_active_channels": "128", //Optional. Concurrently active device transfer channels in CS scenarios, in [1, 8192]. Default: 128.
    "comm_resource_config.multi_channel.num_workers": 1, //Optional. Worker count for concurrent multichannel transfers, in [1, 16]. Default: 1.
    "comm_resource_config.multi_channel.split_batch_size": 128 //Optional. Buffer splitting granularity for concurrent multichannel transfers, in [1, 4294967295]. Default: 128.
}
```

<a id="grc_nic_topo_path"></a>

- **nic_topo_path** (`comm_resource_config.nic_topo_path`): Specifies the affinity between Host RoCE NICs and Devices as a path to a Host NIC topology JSON file. An empty string is treated as unconfigured. This field applies only when LocalCommRes is not specified. If local communication resources have been provided through `LocalCommRes` or `local_comm_res_path`, this field is not read, and its path is not validated.
  - If a nonempty path is configured, the JSON file specifies the affinity. The target must be a regular file with a size in [1 byte, 1MiB]. **Initialize** fails if the file does not exist, is not a regular file, is empty, exceeds the size limit, or cannot be opened. The JSON content is validated when Host RoCE endpoints are generated. Invalid content causes endpoint generation to fail.
  - If this field is omitted or empty, the DCMI API is used to attempt to detect NIC affinity. The hixl engine automatically selects a Host RoCE NIC based on DCMI detection results only for server and pod product forms with HDK `26.2.RC1.B082` or later and 1825 `SP560 1.0.1.1.B031` or later. Generation fails if DCMI is unsupported or no usable IPv4 NIC can be selected.
  - The JSON file contains an object whose members each describe the NIC affinity for one NPU.

| Field | Meaning | Constraints |
| --- | --- | --- |
| key | `npu:<user_device_id>` | `user_device_id` is the user-visible device ID returned by `aclrtGetDevice` and affected by `ASCEND_RT_VISIBLE_DEVICES`. It must be a nonnegative decimal integer. For duplicate keys, the later value takes precedence. |
| value | Array of length 2: `[preferred, avail]` | `preferred` specifies the preferred NIC; only 1 NIC can be configured. The NIC name is the ibverbs device name, such as `hrn5_0`. Use `hiroce5 gids` to query the ib_dev name for the NPU. The name must be nonempty and no longer than 63 characters. `avail` specifies alternative NICs. It is reserved and currently cannot be configured; set it to an empty array `[]`. |

Example:

```json
{
    "comm_resource_config.nic_topo_path": "/path/to/nic_topo.json",
    "comm_resource_config.protocol_desc": ["roce:host"]
}
```

Example contents of `nic_topo.json`:

```json
{
    "npu:0": [["hrn5_0"], []],
    "npu:1": [["hrn5_1"], []]
}
```

**Transfer Batches (`transfer_config.*`)**

<a id="grc_transfer_batch"></a>

- **max_transfer_count_per_batch**: Maximum number of buffers per internal transfer batch. If this limit is exceeded, HIXL automatically splits buffers into batches while preserving order. Default: 1920. HIXL validates against [1, 32766] globally and [1, 1920] for HCCS/FabricMem. Both FabricMem modes validate against [1, 1920]: with `fabric_memory.enable_aicpu_unfold=true`, this value controls logical AI CPU unfolding batches and Notify boundaries; with `false`, the Host submits `aclrtMemcpyAsync` calls consecutively without batching or synchronizing at this boundary. RoCE/URMA queue depth: Client SQ/SCQ depth is calculated as `max(64, nextPowerOfTwo(configured_value + 2))`; Server SQ/SCQ depth is fixed at 64. HIXL does not configure RQ/RCQ depth; Hcomm applies protocol and platform defaults. Hcomm validates queue depths against protocol and hardware capabilities. Supported in CANN 9.2.0 and later.

Example:

```sh
{
    "transfer_config.max_transfer_count_per_batch": 1920 //Optional. Maximum buffers per internal transfer batch. Default: 1920.
}
```

**Fabric Mem (`fabric_memory.*`)**

<a id="grc_fabric_memory"></a>

- **max_capacity**: Virtual memory pool size, an integer in (0, 1024], in TB. Default: 32. The underlying component determines the usable range.
- **start_address**: Virtual memory pool starting address, an integer in [0, 1024], in TB. Default: 40.
- **task_stream_num**: Streams used by a task, in [1, 8]. Default: 1. Only 1 is supported when **enable_aicpu_unfold** is **true**.
- **enable_aicpu_unfold**: Whether AI CPU unfolds tasks. Boolean, default **true**.

Example:

```sh
{
    "fabric_memory.max_capacity": "128", //Virtual memory pool size, an integer in (0, 1024], in TB. Default: 32. The underlying component determines the usable range.
    "fabric_memory.start_address": "40", //Virtual memory pool starting address, an integer in [0, 1024], in TB. Default: 40.
    "fabric_memory.task_stream_num": "1", //Streams used by a task, in [1, 8]. Default: 1. Only 1 is supported when enable_aicpu_unfold is true.
    "fabric_memory.enable_aicpu_unfold": true //Whether AI CPU unfolds tasks. Boolean, default true.
}
```

**Asynchronous Connection/Disconnection Thread Pool (`connect_pool.*`)**

<a id="grc_connect_pool"></a>

- **thread_num**: Connection thread pool size, an integer in [1, 64]. Default: 2.
- **task_queue_capacity**: Connection task queue capacity, an integer in [1, 65535]. Default: 128.

Example:

```sh
{
    "connect_pool.thread_num": "2", //Connection thread pool size, an integer in [1, 64]. Default: 2.
    "connect_pool.task_queue_capacity": "256" //Connection task queue capacity, an integer in [1, 65535]. Default: 128.
}
```

**Control Plane TLS (`tls_config.*`)**

<a id="grc_tls_config"></a>

All HIXL instances in the same process must use the same control plane TLS configuration, including whether TLS is enabled. Otherwise, initialization returns a parameter error. To change the configuration, first destroy all related objects in the process, and then reinitialize them.

- **cert_file**: Path to the control plane TCP TLS certificate file. String; required when TLS is enabled. It must be configured together with key_file. Otherwise, **Initialize** returns a parameter error without falling back to plaintext. TLS is disabled if no `tls_config.*` fields are configured.
- **key_file**: Path to the control plane TCP TLS private key file. String; required when TLS is enabled. It must be configured together with cert_file. Otherwise, **Initialize** returns a parameter error without falling back to plaintext.
- **ca_file**: Path to the CA file used to verify the peer certificate. String; optional, but required when verify_peer is true.
- **verify_peer**: Whether to require and verify a peer certificate. Boolean; optional; defaults to false. The control plane establishes connections by IP address and does not perform hostname/SNI verification.
- **ciphers**: OpenSSL cipher suites. String; optional; applies only to TLS 1.2. For example, `ECDHE-RSA-AES256-GCM-SHA384`. An invalid value causes **Initialize** to return a parameter error.
- **enable_cert_refresh**: Whether to automatically load changed certificate files. Boolean; optional; defaults to false. When enabled, a successfully loaded new certificate applies only to connections established afterward; existing connections are unaffected. If loading fails, the original certificate remains in use. When disabled, changes to certificate files do not take effect automatically.

Example:

```jsonc
{
    "tls_config.cert_file": "/path/to/cert.pem", // Required when TLS is enabled. Certificate file path; must be configured together with key_file.
    "tls_config.key_file": "/path/to/key.pem", // Required when TLS is enabled. Private key file path; must be configured together with cert_file.
    "tls_config.ca_file": "/path/to/ca.pem", // Optional. CA file path for peer certificate verification; required when verify_peer is true.
    "tls_config.verify_peer": true, // Optional. Whether to require and verify a peer certificate. Boolean; defaults to false.
    "tls_config.enable_cert_refresh": true // Optional. Whether to automatically load changed certificate files. Boolean; defaults to false.
}
```

**Connection Pool (`channel_pool.*`)**

<a id="grc_channel_pool"></a>

- **max_channel**: Maximum number of connections. An integer in (0, 512]; defaults to 512 in communicator mode. In HIXL_CS mode, no limit applies if this field is omitted.
- **high_waterline**: High watermark for connection reclamation. A decimal in (0, 1); must be configured together with channel_pool.low_waterline. Applies only in communicator mode.
- **low_waterline**: Low watermark for connection reclamation, specifying the proportion of connections retained after reclamation. A decimal in (0, 1); must be lower than and configured together with the high watermark. Applies only in communicator mode.

In HIXL_CS mode, configure only max_channel; the high and low watermarks have no effect. The local endpoint retains at most max_channel connections as a client. When the total number of established connections and in-flight connection attempts reaches max_channel, the least recently used idle connection is destroyed before a new connection is established. Connections with transfers in progress are not destroyed. If all pooled connections have transfers in progress, or in-flight connection attempts occupy all slots, the current connection attempt returns RESOURCE_EXHAUSTED. An explicit Connect call reestablishes a destroyed connection.

In communicator mode, when the number of connections reaches the high watermark, the connection pool selects (current connection count - connection count at the low watermark) connections for destruction. Connections with transfers in progress are not destroyed. The parameters are calculated as follows:

```sh
Connection count at the high watermark = max(1, floor(channel_pool.max_channel × channel_pool.high_waterline))
Connection count at the low watermark = max(1, floor(channel_pool.max_channel × channel_pool.low_waterline))
```

In the following example, the high watermark corresponds to 3 connections and the low watermark to 1. Before each connection attempt, the pool checks whether there are at least 3 connections. If so, it selects (current connection count - 1) connections for destruction. Connections transferring data are not destroyed.

Example in communicator mode:

```sh
{
    "channel_pool.max_channel": "10", //Maximum connections, an integer in (0, 512]. Default: 512.
    "channel_pool.high_waterline": "0.3", //High watermark for connection reclamation, a decimal in (0, 1). Configure together with channel_pool.low_waterline.
    "channel_pool.low_waterline": "0.1" //Low watermark, the proportion of connections retained after reclamation. A decimal in (0, 1), lower than the high watermark and configured together with it.
}
```

Example in HIXL_CS mode, with the local endpoint maintaining connections to at most 100 remote engines:

```cpp
options[hixl::OPTION_GLOBAL_RESOURCE_CONFIG] = R"({"channel_pool.max_channel": "100"})";
```

- Connection pool constraints:
  - When the connection pool is enabled, configure this option for every HIXL Engine in the cluster.
  - When TransferSync or TransferAsync is called, the connection pool automatically establishes a connection if none is available.
  - The pool introduces additional transfer and connection establishment overhead, which may degrade performance.
  - High/low watermark reclamation can be configured only in communicator mode. Other modes, including HIXL_CS, support only max_channel.
  - In HIXL_CS mode, the connection pool limits only connections established by the local endpoint as a client, not connections accepted by it as a server.

**Local Communication Resource Files (Used with [LocalCommRes](#option_local_comm_res))**

<a id="grc_local_comm_res_path"></a>

- **local_comm_res_path**: Local communication resource JSON file path, with the same content format as `LocalCommRes`. Supports absolute paths and paths relative to the process's current working directory. The target must be a regular file between 1 byte and 1MiB. If both this field and a nonempty `LocalCommRes` are configured, `LocalCommRes` takes precedence.

<a id="grc_topo_file_path"></a>

- **topo_file_path**: Hardware topology JSON file path for automatic LocalCommRes generation. If omitted or empty, HIXL searches the default topology directory by **mainboard_id**. Otherwise, it uses the specified file. The target must be a regular file between 1 byte and 1MiB. Missing files, invalid types, invalid sizes, or parsing failures cause **Initialize** to fail. If a nonempty **endpoint_list** is already provided through `LocalCommRes` or `local_comm_res_path` (manual **local_comm_res** configuration), this field is neither read nor validated.

Example:

```json
{
    "local_comm_res_path": "/path/to/local_comm_res.json"
}
```

```json
{
    "topo_file_path": "/path/to/topo.json"
}
```

<a id="option_local_comm_res"></a>

**LocalCommRes (OPTION_LOCAL_COMM_RES)**

- Option key: `"LocalCommRes"`.
- Value: JSON string describing local communication resources.
- Default: Not configured.
- Purpose: Supplies locally available communication resources to HIXL. Together with `local_comm_res_path`, `topo_file_path`, and `comm_resource_config.protocol_desc` in `GlobalResourceConfig`, determines the local communication resources actually used. Fields are listed below.

Local communication resource provisioning methods and their precedence are as follows:

| Source | Configuration Location | Description |
| --- | --- | --- |
| Inline JSON | `options["LocalCommRes"]` (this option) | Pass local communication resource JSON directly. |
| JSON file | `local_comm_res_path` in `GlobalResourceConfig` | Specify a file path. HIXL reads the file content, which uses the same format as this option. If both are configured and this option is nonempty, this option takes precedence. |
| Automatic generation | Neither of the above configured | Generated automatically by HIXL. For supported products and constraints, see [version](#lcr_version). |

<a id="communication-resource-configuration-fields"></a>

**Table: LocalCommRes Fields**

| Field | Data Type | Required/Optional | Overview |
| --- | --- | --- | --- |
| version | String | Required | Version determining how connections are established. See [version](#lcr_version). |
| net_instance_id | String | Required | Unique identifier of the current supernode. See [net_instance_id](#lcr_net_instance_id). |
| server_id | String | Optional | Local server identifier. See [server_id](#lcr_server_id). |
| endpoint_list | Array | Required | Available communication devices. See [endpoint_list](#lcr_endpoint_list). |
| endpoint_list[].protocol | String | Required | Communication protocol. See [endpoint_list](#lcr_endpoint_list). |
| endpoint_list[].comm_id | String | Required | Communication identifier. See [endpoint_list](#lcr_endpoint_list). |
| endpoint_list[].placement | String | Required | Communication device placement. See [endpoint_list](#lcr_endpoint_list). |
| endpoint_list[].plane | String | Optional | Communication device plane. See [endpoint_list](#lcr_endpoint_list). |
| endpoint_list[].dst_eid | String | Optional | ${eid} of the peer communication device connected to the current device. See [endpoint_list](#lcr_endpoint_list). |

**Details and Examples**

<a id="lcr_version"></a>

- **version**: Version determining how connections are established. The following configurations are supported:
  <!-- npu="A3,910b" id62 -->
  - `"1.0"` or `"1.2"` (ranktable format): Establishes connections using collective communication communicators. Keeping connections per card at 512 or fewer is recommended. Configure only the Device information used by the current llm datadist in ranktable; `server_count` and `rank_id` are unnecessary.
  <!-- end id62 -->
  - `"1.3"` (recommended): Uses HixlCS to establish connections, with no limit on the number of connections by default. Use channel_pool.max_channel to limit connections initiated by the local endpoint as a client. Only `version` needs to be configured; other fields are generated automatically. Requires HDK 25.5.0 or later and toolkit 9.1.0 or later.

Products and constraints for automatic generation:

<!-- npu="A3,910b" id52 -->
- Atlas A2 and Atlas A3 products: HIXL generates the configuration when omitted or empty and establishes connections using collective communication communicators. Device Stream resources are limited, and connections consume memory, so keeping connections per card at 512 or fewer is recommended.
<!-- end id52 -->
<!-- npu="950" id53 -->
- Ascend 950PR&950DT products: HIXL generates the configuration when omitted or empty. For notes on automatic generation, see Notes for Ascend 950PR&950DT Products below.
<!-- end id53 -->

<a id="lcr_net_instance_id"></a>

- **net_instance_id**: Unique identifier of the current supernode. It only needs to be unique for each supernode.

<a id="lcr_server_id"></a>

- **server_id**: Local server identifier used to determine whether a transfer crosses servers. Determines whether H2H transfers leave the enclosure and applies only when **protocol_desc** is **ub_ctp:host**. You can configure **server_id** manually. If omitted, it is generated automatically when **protocol_desc** contains `ub_ctp:host`.

<a id="lcr_endpoint_list"></a>

- **endpoint_list**: Available communication devices, with the following element fields.
  - `protocol`: Communication protocol. Supported values: `"roce"`, `"ub_ctp"`, `"uboe"`, and `"ub_rtp"`.
  - `comm_id`: Communication identifier. Use ${eid} for **ub_ctp/ub_rtp**, an IPv4/IPv6 NIC address for **roce**, or the Device UBoE NIC IP address for **uboe**.
  - `placement`: Communication device placement. Supported values: `"host"` and `"device"`.
  - `plane`: Optional communication device plane. For **ub_ctp** devices with separate planes, specify a unique value for each plane, such as `"plane-a"`/`"plane-b"`.
  - `dst_eid`: Optional ${eid} of the peer communication device connected to the current device. For **ub_ctp**, specify the peer ${eid} if a full-mesh directly connected peer exists.
  - Interaction with `protocol_desc`: `comm_resource_config.protocol_desc` in `GlobalResourceConfig` filters this list by protocol and placement, for both inline and automatically generated configurations.

<!-- npu="950" id54 -->
**Notes for Ascend 950PR&950DT Products**

1. Automatic LocalCommRes generation requires LCNE `LCNE: UBM_2.0.0.B011` or later (run `dis startup` on the 1213 frontend to check) and HDK `25.1.RC1.B108` or later (run `npu-smi info` to check).
2. Currently, only UB scenarios support automatic generation of `net_instance_id` and `endpoint_list`. To configure LocalCommRes yourself, use the tool to assist in generating information for a specified NPU. See [scripts/tools/hixl_tool/readme.md](../../../../scripts/tools/hixl_tool/readme.md).
3. For UB, if `endpoint_list` contains only UB endpoints with **placement=device**, only Device address registration and transfers are supported. With only **placement=host** endpoints, only Host addresses are supported. To use both Device and Host addresses, configure UB endpoints for both placements.
<!-- end id54 -->

**Configuration Examples**

UB: Minimal configuration (only `version`; other fields are generated automatically):

```json
{
    "version": "1.3"
}
```

UB: Complete configuration:

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "server_id": "server_0",
  "endpoint_list": [
    {
      "protocol": "ub_ctp",
      "comm_id": "00000000007f020000100000df149001",
      "placement": "host",
      "dst_eid": "00000000007f030000100000df141c01"
    }
  ]
}
```

ROCE:

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "roce",
      "comm_id": "192.168.100.100",
      "placement": "host"
    }
  ]
}
```

UBOE:

```json
{
  "version": "1.3",
  "net_instance_id": "superpod1_1",
  "endpoint_list": [
    {
      "protocol": "uboe",
      "comm_id": "192.168.100.123",
      "placement": "device"
    }
  ]
}
```

UB_RTP:

```json
{
  "version": "1.3",
  "net_instance_id": "superpod_1",
  "endpoint_list": [
    {
      "protocol": "ub_rtp",
      "comm_id": "0000000000ff0a80000000000a140200",
      "placement": "device"
    }
  ]
}
```

> **Note:** Values in these examples illustrate the format only. Replace them with actual communication resource information queried from your environment. Copying the example values directly causes communication failures.

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

1. Use this API together with **Finalize**. After successful initialization, call **Finalize** before any exit to release resources. Otherwise, resources may be released in an unexpected order, causing errors.
2. Call **aclrtSetDevice** before initialization.
3. Calling **Initialize** again after successful initialization is idempotent: it returns **SUCCESS** without recreating the engine, listening again, or applying parameter changes. Call **Finalize** first to reinitialize with new parameters.

## Finalize

**Description**

Cleans up HIXL resources.

**Prototype**

```cpp
void Finalize()
```

**Parameters**

None

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

None

**Exception Handling**

None

**Constraints**

- Use this API together with **Initialize**.
- It is recommended that you disconnect all connections and deregister registered memory before calling **Finalize**.
- The Server must wait for all Clients to disconnect before calling this API. If the Server exits earlier, errors occur during Client disconnection and data transfer.
- If a Client reads or writes remote Server addresses, the Server must wait until the Client finishes these operations before calling this API. Otherwise, the operations fail.
- This API cannot be called concurrently with other APIs.

## RegisterMem

**Description**

Registers local and remote memory addresses used by **TransferSync**. Addresses specified in **TransferSync** may be subsets of the registered regions. Register local memory with the current HIXL and remote memory with the remote HIXL. Repeated **RegisterMem** calls for the same region (same **addr** and **len**) return **SUCCESS** and the original **mem_handle**, without creating new underlying resources.

**Prototype**

```cpp
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| mem | Input | Description of the memory to register. Type: **MemDesc**. |
| type | Input | Type of the memory to register. Type: **MemType**. |
| mem_handle | Output | Memory handle returned after successful registration, used for memory deregistration. Type: **MemHandle**. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

- Register all local memory before calling **Connect** to establish a connection with the peer.
- It is recommended that you register no more than 4K memory regions per **Hixl** instance. Excessive registrations may cause device OOM and increase connection establishment time, potentially causing connection timeouts. Control the number and size of registered regions based on your application.
<!-- npu="A3,910b" id8 -->
- Up to 50GB of Device memory can be registered. The Host memory limit is 20GB for HDK earlier than 25.5 and 1TB for HDK 25.5 or later. Registering more memory consumes more OS memory. This constraint applies to the following products:
  <!-- npu="910b" id9 -->
  - Atlas A2 products
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3 products
  <!-- end id10 -->
<!-- end id8 -->
  <!-- npu="A3,910b" id11 -->
- Allocate Host memory to register using **aclrtMallocHost**, which automatically aligns addresses. This constraint applies to the following products:
  <!-- npu="910b" id12 -->
  - Atlas A2 products
  <!-- end id12 -->
  <!-- npu="A3" id13 -->
  - Atlas A3 products
  <!-- end id13 -->
<!-- end id11 -->
<!-- npu="A3" id35 -->
- Host memory in FabricMem has these constraints: HDK 25.5 does not support `aclrtMemRetainAllocationHandle`, so allocate with ADXL `AdxlEngine::MallocMem` and free with `AdxlEngine::FreeMem`. HDK 26.0 and later support direct management through ACL APIs. This constraint applies to the following products:
  <!-- npu="A3" id36 -->
  - Atlas A3 products
  <!-- end id36 -->
<!-- end id35 -->
- Device memory to be registered must be allocated using **aclrtMalloc**. For HCCS transfers, set the allocation policy to **ACL\_MEM\_MALLOC\_HUGE\_ONLY**.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
<!-- npu="950" id14 -->
- On Ascend 950PR&950DT products, Host RoCE NICs currently do not support registering memory allocated by **aclrtMallocHost**. Use **malloc** or another method.
<!-- end id14 -->

## DeregisterMem

**Description**

Deregisters memory. Repeated **DeregisterMem** calls with the same **mem_handle** release resources on the first call; later calls return **SUCCESS** without action. A non-null handle not obtained through **RegisterMem** also returns **SUCCESS** without action. **nullptr** returns **PARAM_INVALID**.

**Prototype**

```cpp
Status DeregisterMem(MemHandle mem_handle)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| mem_handle | Input | Memory handle returned by **RegisterMem** during memory registration. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

- Before calling this API, call **Disconnect** to disconnect all connections and ensure that all memory is no longer in use.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## Connect

**Description**

Establishes a connection with a remote HIXL.

**Prototype**

```cpp
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| timeout_in_millis | Input | Connection establishment timeout, in ms. Default: 1000. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **TIMEOUT**: Connection establishment timed out.
- **ALREADY\_CONNECTED**: Already connected.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

- Call this API after **Initialize** completes on both the Client and Server.
  <!-- npu="A3,910b" id15 -->
- When **OPTION_LOCAL_COMM_RES** is empty or its **version** is **"1.0"** or **"1.2"**, connections use collective communication communicators, with a maximum of 512 communications. Excessive connections may cause memory OOM and degrade KV Cache transfer performance. This constraint applies to the following products:
  <!-- npu="910b" id16 -->
  - Atlas A2 products
  <!-- end id16 -->
  <!-- npu="A3" id17 -->
  - Atlas A3 products
  <!-- end id17 -->
  <!-- end id15 -->
- When OPTION_LOCAL_COMM_RES has version "1.3" (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later), HixlCS is used to establish connections, with no limit on the number of connections by default. Use channel_pool.max_channel to limit connections initiated by the local endpoint as a client.
- A timeout of at least 200ms is recommended.
- Register all local and remote memory before calling this API. Memory registered after connection establishment does not support remote access.
  <!-- npu="A3,910b" id18 -->
- In containers, map **/etc/hccn.conf** into the container or ensure that **hccn_tool** exists under **/usr/local/Ascend/driver/tools**. If neither is possible, add its directory to **PATH**. In the example below, **hccn_tool_install_path** is that directory. This constraint applies to the following products:
  <!-- npu="910b" id19 -->
  - Atlas A2 products
  <!-- end id19 -->
  <!-- npu="A3" id20 -->
  - Atlas A3 products
  <!-- end id20 -->

  ```sh
  export PATH=$PATH:${hccn_tool_install_path}
  ```
  <!-- end id18 -->

<!-- npu="A3,910b" id38 -->
- For Device RoCE, use consistent Device RoCE address configurations throughout the communication cluster. Mixing IPv6-only nodes with IPv4/IPv6 dual-stack nodes is unsupported. This constraint applies to the following products:
  <!-- npu="910b" id39 -->
  - Atlas A2 products
  <!-- end id39 -->
  <!-- npu="A3" id40 -->
  - Atlas A3 products
  <!-- end id40 -->
<!-- end id38 -->

<!-- npu="A3,910b" id41 -->
- For Device RoCE, HIXL obtains RoCE device IP addresses as follows:
  - **IPv4 First**: Uses IPv4 when both IPv4 and IPv6 addresses exist.
  - **IPv6 Fallback**: Uses IPv6 automatically when the device has no IPv4 address but has an IPv6 address.
  - **No Additional Configuration**: No new options or explicit address family are required. HIXL obtains device IP addresses from **hccn.conf** or **hccn_tool** automatically.
  This constraint applies to the following products:
  <!-- npu="910b" id42 -->
  - Atlas A2 products
  <!-- end id42 -->
  <!-- npu="A3" id43 -->
  - Atlas A3 products
  <!-- end id43 -->
<!-- end id41 -->

- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## Disconnect

**Description**

Disconnects from a remote HIXL.

**Prototype**

```cpp
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| timeout_in_millis | Input | Disconnection timeout, in ms. Default: 1000. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- Other values: Failure.

**Constraints**

- Call **Initialize** to complete initialization before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## ConnectAsync

**Description**

Establishes a connection with a remote HIXL asynchronously.

**Prototype**

```cpp
Status ConnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| timeout_in_millis | Input | Connection establishment timeout, in ms. Default: 1000. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout_in_millis <= 0**).
- **RESOURCE\_EXHAUSTED**: Task queue full.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

- This API inherits all constraints of the **Connect** API.
- Configure the thread pool size and task queue length through the **Initialize** API of **Hixl**.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- Do not mix **ConnectAsync/DisconnectAsync** with **Connect/Disconnect**.
- Multiple tasks for the same **remote_engine** execute in submission order. Tasks for different **remote_engine** values may execute concurrently. The queried status is that of the most recently submitted task.

## DisconnectAsync

**Description**

Disconnects from a remote HIXL asynchronously.

**Prototype**

```cpp
Status DisconnectAsync(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| timeout_in_millis | Input | Disconnection timeout, in ms. Default: 1000. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout_in_millis <= 0**).
- **RESOURCE\_EXHAUSTED**: Task queue full.
- Other values: Failure.

**Constraints**

- This API inherits all constraints of the **Disconnect** API.
- Configure the thread pool size and task queue length through the **Initialize** API of **Hixl**.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- Do not mix **ConnectAsync/DisconnectAsync** with **Connect/Disconnect**.
- Multiple tasks for the same **remote_engine** execute in submission order. Tasks for different **remote_engine** values may execute concurrently. The queried status is that of the most recently submitted task.

## GetAsyncConnectStatus (Querying a Specific Connection)

**Description**

Queries a specific asynchronous connection status.

**Prototype**

```cpp
Status GetAsyncConnectStatus(const AscendString &remote_engine, AsyncConnectStatus &status)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| status | Output | Asynchronous connection status, with the following enum values.<br><br>- NOT_CONNECT: Not connected.<br>- CONNECT_PENDING: Connection establishment pending.<br>- CONNECTING: Connection establishment in progress.<br>- CONNECTED: Connection established.<br>- CONNECT_FAILED: Connection establishment failed.<br>- DISCONNECT_PENDING: Disconnection pending.<br>- DISCONNECTING: Disconnection in progress. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

- Call **Initialize** to complete initialization before calling this API.
- The return value indicates only whether the API call succeeded. The output parameter indicates the asynchronous connection/disconnection task status.

## GetAsyncConnectStatus (Querying All Connection Statuses)<a id="getasyncconnectstatus-querying-all-connections"></a>

**Description**

Queries all asynchronous connection statuses.

**Prototype**

```cpp
Status GetAsyncConnectStatus(std::map<AscendString, AsyncConnectStatus> &statuses)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| statuses | Output | Asynchronous connection status, with the following enum values.<br><br>- NOT_CONNECT: Not connected.<br>- CONNECT_PENDING: Connection establishment pending.<br>- CONNECTING: Connection establishment in progress.<br>- CONNECTED: Connection established.<br>- CONNECT_FAILED: Connection establishment failed.<br>- DISCONNECT_PENDING: Disconnection pending.<br>- DISCONNECTING: Disconnection in progress. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

- Call **Initialize** to complete initialization before calling this API.
- The return value indicates only whether the API call succeeded. The output parameter indicates the asynchronous connection/disconnection task status.

## TransferSync

**Description**

Transfers memory with a remote HIXL.

**Prototype**

```cpp
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| operation | Input | Reads remote memory into local memory or writes local memory to remote memory. |
| op_descs | Input | Local and remote addresses for batch operations. |
| timeout_in_millis | Input | Transfer timeout, in ms. Default: 1000. |

**Example**

See [Running the Examples](../../../../examples/cpp/README_en.md).

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- **TIMEOUT**: Transfer timed out.
- **RESOURCE_EXHAUSTED**: Resources exhausted.
- Other values: Failure.

**Constraints**

  <!-- npu="A3,910b" id21 -->
- Before calling this API, call **Connect** to establish a connection or enable the connection pool during HIXL initialization through **OPTION_GLOBAL_RESOURCE_CONFIG** in **options**. This constraint applies to the following products:
  <!-- npu="910b" id22 -->
  - Atlas A2 products
  <!-- end id22 -->
  <!-- npu="A3" id23 -->
  - Atlas A3 products
  <!-- end id23 -->
  <!-- end id21 -->
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
  <!-- npu="A3,910b" id24 -->
- The staging memory pool is enabled by default. With it enabled, a staging transfer is used if either local or remote memory in **op\_desc** is unregistered. Unregistered memory is treated as Host memory; ensure that its address is valid. This constraint applies to the following products:
  <!-- npu="910b" id25 -->
  - Atlas A2 products
  <!-- end id25 -->
  <!-- npu="A3" id26 -->
  - Atlas A3 products
  <!-- end id26 -->
  <!-- end id24 -->
  <!-- npu="A3,910b" id27 -->
- In staging mode, all **op\_desc** entries must use the same transfer type, for example, writing local Host memory to remote Host memory. This constraint applies to the following products:
  <!-- npu="910b" id28 -->
  - Atlas A2 products
  <!-- end id28 -->
  <!-- npu="A3" id29 -->
  - Atlas A3 products
  <!-- end id29 -->
  <!-- end id27 -->
  <!-- npu="A3" id30 -->
- In Fabric Mem transfer mode, all op_descs must have the same transfer type. The system determines the transfer direction from the memory type of the first op_desc. This constraint applies to the following products:
  - Atlas A3 products
  <!-- end id30 -->
  <!-- npu="A3,910b" id81 -->
- When Fabric Mem mode is disabled, the transfer directions supported by direct HCCS transfers depend on the chip model. Atlas A2 products support only D2rD and rD2D; Atlas A3 products support D2rD, rD2D, H2rD, and rD2H. Direct HCCS transfers do not support D2rH, rH2D, H2rH, or rH2H. To use HCCS for D2rH or rH2D transfers on Atlas A3 products, enable Fabric Mem mode (see [EnableUseFabricMem](#option_enable_use_fabric_mem)). For measured bandwidth in each direction for each protocol, see [Communication Performance Data](../../../../benchmarks/performance.md). This constraint applies to the following products:
  <!-- npu="910b" id82 -->
  - Atlas A2 products
  <!-- end id82 -->
  <!-- npu="A3" id83 -->
  - Atlas A3 products
  <!-- end id83 -->
<!-- end id81 -->

## TransferAsync

**Description**

Performs batch asynchronous memory transfers with a remote HIXL.

**Prototype**

```cpp
  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote HIXL. |
| operation | Input | Reads remote memory into local memory or writes local memory to remote memory. |
| op_descs | Input | Local and remote addresses for batch operations. |
| optional_args | Input | Optional parameters. **user_data** is recorded with the request and returned unchanged in **TransferResult.user_data** by the batch **GetTransferStatus** API. |
| req | Output | Request handle used to query transfer request status. |

**Example**

```cpp
  //Initialize the client and server engines and establish a connection.
  client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
```

**Return Value**

- **SUCCESS**: Success.
- **NOT\_CONNECTED**: No connection to the peer.
- **RESOURCE_EXHAUSTED**: Resources exhausted.
- Other values: Failure.

**Constraints**

- The following constraints apply before calling this API:
  - Call **Connect** to establish a connection with the peer first.
  <!-- npu="A3,910b" id31 -->
  - Alternatively, enable the connection pool during HIXL initialization by configuring **OPTION_GLOBAL_RESOURCE_CONFIG** in **options**. This constraint applies to the following products:
    <!-- npu="910b" id32 -->
    - Atlas A2 products
    <!-- end id32 -->
    <!-- npu="A3" id33 -->
    - Atlas A3 products
    <!-- end id33 -->
  <!-- end id31 -->
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- Currently, asynchronous transfers support only direct transfers. Staging transfers are unsupported. Direct transfer is the default.
  <!-- npu="A3" id34 -->
- In Fabric Mem transfer mode, all op_descs must have the same transfer type. The system determines the transfer direction from the memory type of the first op_desc. This constraint applies to the following products:
  - Atlas A3 products
  <!-- end id34 -->
  <!-- npu="A3,910b" id84 -->
- When Fabric Mem mode is disabled, the transfer directions supported by direct HCCS transfers depend on the chip model. Atlas A2 products support only D2rD and rD2D; Atlas A3 products support D2rD, rD2D, H2rD, and rD2H. Direct HCCS transfers do not support D2rH, rH2D, H2rH, or rH2H. To use HCCS for D2rH or rH2D transfers on Atlas A3 products, enable Fabric Mem mode (see [EnableUseFabricMem](#option_enable_use_fabric_mem)). For measured bandwidth in each direction for each protocol, see [Communication Performance Data](../../../../benchmarks/performance.md). This constraint applies to the following products:
  <!-- npu="910b" id85 -->
  - Atlas A2 products
  <!-- end id85 -->
  <!-- npu="A3" id86 -->
  - Atlas A3 products
  <!-- end id86 -->
<!-- end id84 -->

## GetTransferStatus (Querying a Specific Transfer Request)

**Description**

Queries the status of an asynchronous memory transfer.

**Prototype**

```cpp
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| req | Input | Request handle generated by **TransferAsync**. |
| status | Output | Transfer status, with the following enum values.<br><br>- WAITING<br>- COMPLETED<br>- TIMEOUT (currently unsupported)<br>- FAILED |

**Example**

```cpp
  //Initialize the client and server engines and establish a connection.
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  //req is the output of TransferAsync(). Use this request handle to query the transfer status.
  Status query_status = client_engine.GetTransferStatus(req, status);
  //Check the transfer status to determine whether the transfer is complete.
  ...
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- After calling **TransferAsync**, use this API to query the request status. If the status is **COMPLETED** or **FAILED**, related resources are released and the request cannot be queried again.
- For asynchronous transfers, determine timeouts in your application. If a task times out, call **Disconnect** to destroy the connection and clean up related resources.

## GetTransferStatus (Querying All Transfer Requests)

**Description**

Queries the status of all asynchronous memory transfers.

**Prototype**

```cpp
  Status GetTransferStatus(const GetTransferStatusArgs &args, std::vector<TransferResult> &results)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| args | Input | Parameters for querying all asynchronous transfer request statuses. |
| results | Output | Statuses of all asynchronous transfer requests. |

**Example**

```cpp
  //Initialize the client and server engines and establish a connection.
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  //req is the output of TransferAsync(). Use this request handle to query the transfer status.
  GetTransferStatusArgs args = { .max_query_count = 4, .skip_waiting = true };
  std::vector<TransferResult> results;
  Status query_status = client_engine.GetTransferStatus(args, results);
  //Check the transfer status to determine whether the transfer is complete.
  ...
```

**Return Value**

- **SUCCESS**: Success.
- **UNSUPPORTED**: Querying through this API is unsupported unless the **options** for Hixl initialization set **LocalCommRes.version** to **1.3** or **GlobalResourceConfig.comm_resource_config.protocol_desc** includes **uboe:device** or **ub_rtp:device**.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- After calling **TransferAsync**, use this API to query all request statuses. If a request is **COMPLETED** or **FAILED**, its resources are released and subsequent queries no longer return that request.
- For asynchronous transfers, determine timeouts in your application. If a task times out, it is recommended that you call **Disconnect** to destroy the connection and clean up related resources.

## SendNotify

**Description**

Sends a Notify message to a remote engine.

**Prototype**

```cpp
  Status SendNotify(const AscendString &remote_engine,
                    const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| remote_engine | Input | Unique identifier of the remote **Hixl**. |
| notify | Input | Notify content to send. Both **notify_msg** and **name** have a maximum length of 1024 bytes, measured in storage bytes by **AscendString.GetLength()**. The character count of a multibyte encoding is not this length. |
| timeout_in_millis | Input | Send timeout, in ms. Default: 1000. |

**Example**

```cpp
// client_engine has been initialized and connected to remote_engine.
NotifyDesc notify;
notify.name = AscendString("cache_ready");
notify.notify_msg = AscendString("block_0");
Status ret = client_engine.SendNotify(remote_engine, notify, 1000);
if (ret != SUCCESS) {
  // Handle the send failure.
}
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**timeout_in_millis <= 0**, or **notify.name/notify_msg** exceeds 1024 bytes).
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- Each connection supports up to 4096 Notify messages. Ensure that the remote **Hixl** promptly calls **GetNotifies** to consume them, preventing send failures caused by reaching the limit.

## GetNotifies

**Description**

Obtains Notify messages received by all Servers in the current **Hixl** and clears the received messages.

**Prototype**

```cpp
  Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| notifies | Output | Vector for storing notify messages. |

**Example**

```cpp
std::vector<NotifyDesc> notifies;
Status ret = server_engine.GetNotifies(notifies);
if (ret != SUCCESS) {
  // Handle the retrieval failure.
}
```

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

None

## GetCapability

**Description**

Queries library capabilities. The upper layer can call this API before **Initialize** to detect whether the current **Hixl** library supports capabilities such as Auto Connect and Client/Server communication, avoiding hard-coded defaults or incompatibility with older **.so** files.

**Prototype**

```cpp
static Status GetCapability(FeatureType feature_type, int32_t &value)
```

**Parameters**

| Parameter | Input/Output | Values |
| --- | --- | --- |
| feature_type | Input | Capability type. For values, see **FeatureType**. |
| value | Output | Capability support status. **1** means supported (**FEATURE_SUPPORTED**); **0** means unsupported (**FEATURE_NOT_SUPPORTED**). |

**Example**

```cpp
int32_t value = FEATURE_NOT_SUPPORTED;
Status ret = Hixl::GetCapability(AUTO_CONNECT, value);
if (ret != SUCCESS) {
  // Handle the query failure.
}
bool supports_auto_connect = (value == FEATURE_SUPPORTED);
```

**Return Value**

- **SUCCESS**: Success. For unknown or unsupported capability types, `value` is `FEATURE_NOT_SUPPORTED`.
- **PARAM_INVALID**: Invalid parameter (**feature_type** is negative).

**Constraints**

- This API can be called without calling **Initialize**.
- Static method independent of a **Hixl** instance.
