# HIXL Interface

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 Training Series/Atlas A3 Inference Series: Supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 Inference Series/Atlas A2 Training Series: Supported
<!-- end id3 -->

Note: For Atlas A2 Training Series/Atlas A2 Inference Series, only Atlas 800I A2 Inference Server and A200I A2 Box heterogeneous components are supported.

## Hixl Constructor

**Function**

Create a Hixl object.

**Prototype**

```python
__init__()
```

**Parameters**

None

**Example**

```python
import hixl
engine = hixl.Hixl()
```

**Return Value**

Returns a Hixl instance on success.

**Constraints**

- If finalize was not called before the Hixl object is destroyed, the destructor will automatically call finalize for resource cleanup. However, it is still recommended to explicitly call finalize to ensure resources are released as expected.

## initialize

**Function**

Initialize HIXL. This must be called before any other interface.

**Prototype**

```python
initialize(local_engine: str, options: Dict[str, str] = {}) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| local_engine | str | HIXL identifier, must be unique across all participating nodes. For IPv4, format is host\_ip:host\_port or host\_ip. For IPv6, format is [host\_ip]:host\_port or [host\_ip]. Loopback IP is not recommended as it may cause conflicts in multi-HIXL scenarios.<br>When host\_port is set and >0, the current HIXL acts as Server and listens on the configured port. If host\_port is not set or <=0, it acts as Client and does not start listening. |
| options | Dict[str, str] | Initialization parameters. See the tables below for details. |

<!-- npu="A3,910b" id5 -->
**Table 1**  options (Atlas A2 Training Series/Atlas A2 Inference Series/Atlas A3 Training Series/Atlas A3 Inference Series)

| Parameter | Optional/Required | Description |
| --- | --- | --- |
| OPTION_ENABLE_USE_FABRIC_MEM | Optional | String value "EnableUseFabricMem".<br>- 0: Do not enable Fabric Mem mode<br>- 1: Enable Fabric Mem mode<br><br>This option applies to scenarios requiring HCCS for D2RH, RH2D transfers.<br><br>Note: In cluster scenarios, this parameter must be configured with the same value on all nodes. This parameter cannot be configured simultaneously with "OPTION_BUFFER_POOL". Only supported on Atlas A3 Training Series/Atlas A3 Inference Series. |
| OPTION_BUFFER_POOL | Optional | String value "BufferPool".<br>In scenarios requiring relay buffer for transfer:<br>- When RDMA registered Host memory size is limited.<br>- When multiple small memory blocks (e.g., 128K) need relay transfer to improve performance.<br>This option can be used to configure relay memory pool size, format is "${BUFFER_NUM}:${BUFFER_SIZE}", system default is "4:8(unit MB)", can be configured as "0:0" to disable relay memory pool. In concurrent scenarios, it is recommended to increase ${BUFFER_NUM}. Additionally, all usage locations must configure the same value. This parameter cannot be configured simultaneously with "OPTION_ENABLE_USE_FABRIC_MEM".<br>Note: When this parameter is not configured, the following constraints apply.<br><br>Atlas A2 Training Series/Atlas A2 Inference Series: Only Atlas 800I A2 Inference Server and A200I A2 Box heterogeneous components are supported. In this scenario, when Server uses HCCS transfer protocol, only D2D is supported. |
| OPTION_RDMA_TRAFFIC_CLASS | Optional | String value "RdmaTrafficClass".<br>Used to configure RDMA NIC traffic class. Same function as environment variable HCCL_RDMA_TC. If both are configured, current option has higher priority; if only one is configured, that configuration takes effect.<br>Value range is [0,255], and must be configured as a multiple of 4, default value is 132. |
| OPTION_RDMA_SERVICE_LEVEL | Optional | String value "RdmaServiceLevel".<br>Used to configure RDMA NIC service level. Same function as environment variable HCCL_RDMA_SL. If both are configured, current option has higher priority; if only one is configured, that configuration takes effect.<br>Value range is [0, 7], default value is 4. |
| OPTION_GLOBAL_RESOURCE_CONFIG | Optional | String value "GlobalResourceConfig". Used to enable and configure global resource configuration. For configuration examples and usage constraints, see below the table. |
| OPTION_AUTO_CONNECT | Optional | String value "AutoConnect".<br>- 0: Do not enable Auto Connect mode<br>- 1: Enable Auto Connect mode<br><br>Note:<br>- After enabling this option, link establishment can be skipped and transfer can proceed directly.<br>- After enabling this option, abnormal links or peer destruction will be automatically cleaned up (peer destruction requires heartbeat mechanism to detect, heartbeat interval defaults to 10s). |
| OPTION_LOCAL_COMM_RES | Optional | Configure local communication resource information, format is json string.<br>- Not configured or configured as empty string: Related information will be automatically generated, using collective communication domain method for link establishment. Due to limited Device-side Stream resources and link establishment occupying memory, it is recommended that single-card link count does not exceed 512. You can also specify local communication resource JSON file path through local_comm_res_path in OPTION_GLOBAL_RESOURCE_CONFIG, HIXL reads file content as local communication resource; when both are configured and this option is non-empty, this option takes precedence.<br>  Note: When OPTION_BUFFER_POOL (or adxl.BufferPool) is configured as "0:0" (relay memory pool disabled) and hcomm/toolkit version is >= 9.1.0, HixlCS capability will be used for link establishment with no link limit.<br>- Configure version as "1.0" or "1.2" ranktable format: Uses collective communication domain method for link establishment. Due to limited Device-side Stream resources and link establishment occupying memory, it is recommended that single-card link count does not exceed 512. Only need to configure Device information used by current llm datadist in ranktable, no need to configure server_count and rank_id fields. For ranktable details, please refer to [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/zh/user_guide/README.md).<br>- Configure version as "1.3" (recommended, requires HDK version >= 25.5.0 and toolkit version >= 9.1.0): Uses HixlCS capability for link establishment with no link limit. Configuration format refers to [Communication Resource Configuration Field Description](#communication-resource-configuration-field-description), only configure version field, other fields will be auto-generated. |

Environment variables in the above table please refer to [Environment Variable Reference](https://gitcode.com/cann/docs/blob/master/docs/zh/env-vars/README.md), ranktable please refer to [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/zh/user_guide/README.md).

<!-- end id5 -->
<!-- npu="A3,910b" id7 -->
OPTION_LOCAL_COMM_RES configured as "1.3" version examples:

- Minimal configuration (only configure version field, other fields auto-generated):

```json
{
    "version": "1.3"
}
```

- Complete configuration example (manually specify communication resource information):

```json
{
    "version": "1.3",
    "net_instance_id": "superpod1_1",
    "endpoint_list": [
        {
            "protocol": "roce",
            "comm_id": "10.10.10.1",
            "placement": "device"
        }
    ]
}
```

> **Note:** It is recommended to use the minimal configuration method, the system will automatically generate local communication resource information. If manual specification is needed, the meaning of each field in endpoint_list please refer to [Communication Resource Configuration Field Description](#communication-resource-configuration-field-description).

OPTION_GLOBAL_RESOURCE_CONFIG configuration examples and usage constraints:

<!-- npu="A3" id6 -->
For Fabric Mem mode (only supported on Atlas A3 Training Series/Atlas A3 Inference Series), the parameter configuration example:

```sh
{
    "fabric_memory.max_capacity": "128", //Size of virtual memory pool. Value range: integer in (0, 1024], default: 32, unit TB, actual usable range determined by underlying layer
    "fabric_memory.start_address": "40", //Virtual memory pool start address. Value range: integer in [0, 1024], default: 40, unit TB
    "fabric_memory.task_stream_num": "1", //Number of streams per task, value range: [1, 8], default: 1; only supports 1 when enable_aicpu_unfold is true
    "fabric_memory.enable_aicpu_unfold": true //Whether AICPU unfolds FabricMem, boolean type, default true
}
```
<!-- end id6 -->

For asynchronous link establishment/disconnection mechanism, the parameter configuration example:

```sh
{
    "connect_pool.thread_num":"2", // Connection pool thread count. Value range: integer in [1, 64], default: 2
    "connect_pool.task_queue_capacity":"256" // Connection pool task queue capacity. Value range: integer in [1, 65535], default: 128
}
```

Device-side NIC default listening port is 16666. In scenarios where multiple processes use the same NIC, you can configure as follows:

```sh
{
    "comm_resource_config.listen_port": "26666", //Optional, value range: integer in [1, 65535]. When not configured, auto-generated ranktable does not carry device_port field
    "comm_resource_config.max_active_channels": "128" //Optional, configure device-side concurrent active transfer channel count in CS scenario. Value range: [1, 8192], default: 128, each active channel consumes 2 Stream resources
}
```

When configuring local communication resource via file path:

```json
{
    "local_comm_res_path": "/path/to/local_comm_res.json"
}
```

`local_comm_res_path` supports absolute and relative paths, relative paths are resolved based on process current working directory. Target file must be a regular file with size in [1 byte, 1 MiB] range, file content format same as OPTION_LOCAL_COMM_RES. When configured simultaneously with OPTION_LOCAL_COMM_RES and option is non-empty, OPTION_LOCAL_COMM_RES takes precedence.

For link pool mechanism, the parameter configuration example:

```sh
{
    "channel_pool.max_channel": "10", //Maximum link count. Value range: integer in (0, 512], default: 512
    "channel_pool.high_waterline": "0.3", //Link reclamation high watermark threshold, value range: decimal in (0, 1), must be configured simultaneously with channel_pool.low_waterline
    "channel_pool.low_waterline": "0.1" //Link reclamation low watermark threshold, i.e., ratio of remaining links after reclamation, value range: decimal in (0, 1), must be less than high watermark and configured simultaneously with it
}
```

When link pool is working, when link count reaches high watermark threshold, select (current link count - link count corresponding to low watermark threshold) links for destruction (links currently transferring will not be destroyed). Related parameter calculations:

```sh
Link count at high watermark = max(1, floor(channel_pool.max_channel × channel_pool.high_waterline))
Link count at low watermark = max(1, floor(channel_pool.max_channel × channel_pool.low_waterline))
```

In the above configuration example, link count at high watermark = 3, link count at low watermark = 1. Before each link establishment, check if current link count has reached 3, if so, select (current link count - 1) links for destruction (links currently transferring will not be destroyed).

Notes when enabling link pool mechanism:

- All Hixl Engines in the cluster must configure OPTION_GLOBAL_RESOURCE_CONFIG.
- When calling transfer_sync or transfer_async interfaces, if no available links exist, link pool will automatically perform link establishment.
- Link pool mechanism introduces additional transfer and link establishment overhead, which may cause performance degradation.
<!-- end id7 -->

<!-- npu="950" id4 -->
**Table 2**  options (Ascend 950PR/Ascend 950DT)

| Parameter | Optional/Required | Description |
| --- | --- | --- |
| OPTION_LOCAL_COMM_RES | Optional | Configure local communication resource information, format is json string. Configuration format refers to [Communication Resource Configuration Field Description](#communication-resource-configuration-field-description), configuring as empty will not auto-generate related information. You can also specify local communication resource JSON file path through local_comm_res_path in OPTION_GLOBAL_RESOURCE_CONFIG, HIXL reads file content as local communication resource. OPTION_LOCAL_COMM_RES configured as non-empty string or local_comm_res_path in OPTION_GLOBAL_RESOURCE_CONFIG configured as valid file path, at least one must be configured. When both are configured and this option is non-empty, this option takes precedence. Configuration examples see [Configuration Examples](#configuration-examples) below.<br/>**Note:<br/>1. Specific values in the above configuration examples are for format reference only, actual usage must query real communication resource configuration information from current environment for replacement, directly copying example values will cause communication failure.<br/>2. Auto-generating localcommres capability requires users to call hixl interface with root permissions, and requires LCNE version not lower than LCNE: UBM_2.0.0.B011, can go to 1213 front desk to execute dis startup to view LCNE version information; HDK version not lower than 25.1.RC1.B108, can view HDK version information via npu-smi info.<br/>3. Currently only UB scenario supports auto-generating net_instance_id and endpoint_list, if users want to configure localcommres information themselves, can use tools to assist in generating localcommres information for specified npu, specific usage method see [scripts/tools/lcrgen/README.md](../../../../scripts/tools/lcrgen/README.md).<br/>4. In UB scenario, if endpoint_list only configures UB endpoint with placement as device, only Device address registration and transfer are supported; if endpoint_list only configures UB endpoint with placement as host, only Host address registration and transfer are supported. When needing to use both Device and Host addresses, must configure UB endpoints with corresponding placement simultaneously.** |
| OPTION_GLOBAL_RESOURCE_CONFIG | Optional | String value "GlobalResourceConfig". Used to enable and configure global resources, format is json string, field description refer to [Global Resource Configuration Field Description](#global-resource-configuration-field-description). |
| OPTION_AUTO_CONNECT | Optional | String value "AutoConnect". Values: 0 — Do not enable Auto Connect mode; 1 — Enable Auto Connect mode. Note: After enabling this option, link establishment can be skipped and transfer can proceed directly; after enabling this option, abnormal links or peer destruction will be automatically cleaned up (peer destruction requires heartbeat mechanism to detect, heartbeat interval defaults to 10s). |
| OPTION_RDMA_TRAFFIC_CLASS | Optional | String value "RdmaTrafficClass".<br>Used to configure RDMA NIC traffic class. Same function as environment variable HCCL_RDMA_TC. If both are configured, current option has higher priority; if only one is configured, that configuration takes effect.<br>Value range is [0,255], and must be configured as a multiple of 4, default value is 132.<br>Note: Applies to Ascend 950PR/Ascend 950DT RoCE scenarios. |
| OPTION_RDMA_SERVICE_LEVEL | Optional | String value "RdmaServiceLevel".<br>Used to configure RDMA NIC service level. Same function as environment variable HCCL_RDMA_SL. If both are configured, current option has higher priority; if only one is configured, that configuration takes effect.<br>Value range is [0, 7], default value is 4.<br>Note: Applies to Ascend 950PR/Ascend 950DT RoCE scenarios. |
<!-- end id4 -->

<a id="configuration-examples"></a>**Configuration Examples**

UB — Minimal configuration (only configure version field, other fields auto-generated)

```json
{
    "version": "1.3"
}
```

UB — Complete configuration

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

ROCE

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

UBOE

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

UB_RTP

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

<a id="communication-resource-configuration-field-description"></a>**Communication Resource Configuration Field Description**

| Field Name | Data Type | Required/Optional | Description | Supported Values/Filling Rules |
| --- | --- | --- | --- | --- |
| version | String | Required | Version number | "1.3". Requires HDK version >= 25.5.0 and toolkit version >= 9.1.0. |
| net_instance_id | String | Required | Unique identifier of current supernode | Unique per supernode. |
| server_id | String | Optional | Current server identifier | Only used for same-OS H2rH loopback judgment when protocol is ub_ctp and placement is host. Not enabled when empty or inconsistent between two ends. |
| endpoint_list | Array | Required | List of usable communication devices | - |
| endpoint_list[].protocol | String | Required | Communication protocol | "roce"/"ub_ctp"/"uboe"/"ub_rtp" |
| endpoint_list[].comm_id | String | Required | Communication identifier | When protocol is ub_ctp/ub_rtp fill `${eid}`; when protocol is roce fill ipv4/ipv6 NIC address; when protocol is uboe fill device uboe NIC ip address |
| endpoint_list[].placement | String | Required | Communication device location | "host"/"device" |
| endpoint_list[].plane | String | Optional | Communication device plane | When protocol is ub_ctp, fill if device distinguishes planes, unique per plane (e.g., "plane-a"/"plane-b") |
| endpoint_list[].dst_eid | String | Optional | `${eid}` of peer communication device connected to current communication device | When protocol is ub_ctp, fill peer `${eid}` if full-mesh direct connection exists |

<a id="global-resource-configuration-field-description"></a>**Global Resource Configuration Field Description**

| Field Name | Data Type | Required/Optional | Description | Supported Values/Filling Rules |
| --- | --- | --- | --- | --- |
| comm_resource_config.protocol_desc | String or string array | Optional | Configure usable communication protocols and communication device location range. UB CTP pure URMA mode can use `ub_ctp`, other configurations use `${protocol}:${placement}` | Supports "ub_ctp"/"roce:device"/"hccs:device"/"ub_ctp:device"/"ub_ctp:host"/"uboe:device"/"ub_rtp:device"/"roce:host". After configuration, will filter explicitly configured endpoint_list in OPTION_LOCAL_COMM_RES and auto-generated endpoint_list by this range. On A5 when this field is not configured or only "ub_ctp:device" is configured, auto-generates Device UB resources, Host memory uses Device UB link transfer after UBMEM maps to Device address; when both "ub_ctp:device" and "ub_ctp:host" are configured, auto-generates Device+Host UB resources and uses pure URMA path; configuring "ub_ctp" is equivalent to above combinations. When configuring "ub_ctp:host" alone, only retains Host UB CTP Endpoint. When manually configuring LocalCommRes, "ub_ctp" requires providing both Device and Host UB CTP Endpoints. Explicitly configured OPTION_LOCAL_COMM_RES does not perform additional filtering when this field is not configured. |
| comm_resource_config.listen_port | JSON number or pure number string | Optional | Configure device-side NIC listening port | Value range [1, 65535]. On Atlas A2 Training Series/Atlas A2 Inference Series, Atlas A3 Training Series/Atlas A3 Inference Series when not configured, fixed use `16666` port; in Ascend 950PR/Ascend 950DT scenario when not configured, underlying communication component auto-selects available port, HIXL auto-queries actual listening port. |
| comm_resource_config.qos | Number | Optional | Configure communication protocol qos | Currently only supports [0-7], when not configured, default is 0. |
| comm_resource_config.max_active_channels | Number | Optional | Configure device-side concurrent active transfer channel count in CS scenario | Value range [1, 8192], default value when not configured is 128. Each active channel consumes 2 Stream resources, configuration value needs to reserve margin based on current card form's Stream resource upper limit and Stream count already created in business; Stream resource upper limits for different card forms see CANN Runtime API [aclrtCreateStream](https://www.hiascend.com/document/detail/zh/canncommercial/latest/API/runtimeapi/aclcppdevg_03_0066.html) documentation. Exceeding [1, 8192] causes initialize to return parameter error. |
| local_comm_res_path | String | Optional | Local communication resource JSON file path; file content format same as OPTION_LOCAL_COMM_RES | Configure absolute or relative path, relative path resolved based on process current working directory. Target file must be a regular file with size in [1 byte, 1 MiB] range. When configured simultaneously with OPTION_LOCAL_COMM_RES and option is non-empty, OPTION_LOCAL_COMM_RES takes precedence. |

**Example**

Refer to [examples](../../../../examples/python/hixl/hixl_d2rd_multiproc_sample.py).

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error
- Others: Failure

**Constraints**

- Must be paired with finalize. After successful initialization, finalize must be called before any exit to ensure proper resource release.
- aclrtSetDevice must be called before initialization.
- Repeated calls to initialize will return SUCCESS and be silently ignored.

## finalize

**Function**

HIXL resource cleanup function.

**Prototype**

```python
finalize() -> None
```

**Parameters**

None

**Example**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
# ... business logic ...
engine.finalize()
```

**Return Value**

None

**Constraints**

- Must be paired with initialize.
- It is recommended to disconnect all links and deregister all memory before calling finalize.
- Server should be called after all Clients have disconnected. If Server exits early, Client disconnection and data transfer will encounter errors.
- When Client needs to operate on Server-side addresses for remote read/write, Server should wait until Client completes remote operations before calling this interface.
- This interface cannot be called concurrently with other interfaces.

## register\_mem

**Function**

Register memory address. Used for transfer\_sync to specify local and remote memory addresses. Addresses specified in transfer\_sync can be a subset of registered addresses. Local memory addresses must be registered in the current HIXL, and remote memory addresses must be registered in the remote HIXL. Repeated calls to register\_mem for the same memory region (same addr and len) will return SUCCESS and the same mem\_handle as the first registration, without creating new underlying resources.

**Prototype**

```python
register_mem(mem_desc: MemDesc, mem_type: MemType) -> Tuple[int, int]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| mem_desc | MemDesc | Description of the memory to be registered. |
| mem_type | MemType | Type of the memory to be registered. |

**Example**

```python
import hixl
engine = hixl.Hixl()
engine.initialize("127.0.0.1:16000")
mem_desc = hixl.MemDesc(addr=dev_addr, len=buf_size)
ret, handle = engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
```

**Return Value**

Returns a tuple (ret, mem\_handle):

- ret: Status code. SUCCESS indicates success, PARAM\_INVALID indicates parameter error, others indicate failure.
- mem\_handle: Memory handle (int type) returned on successful registration, can be used for memory deregistration.

**Constraints**

- All local memory must be registered before calling connect to establish links with remote endpoints.
- It is recommended that a single Hixl instance registers no more than 4K memory regions. Excessive registrations may cause device OOM risk; more registrations also increase link establishment time and may cause timeout issues. Users should manage memory registration count and size based on their business scenarios.
<!-- npu="A3,910b" id8 -->
- Maximum 50GB Device memory can be registered. When HDK version is below 25.5, maximum 20GB Host memory can be registered; when HDK version is 25.5 or above, maximum 1TB Host memory can be registered. Larger registered memory consumes more OS memory. This constraint applies to:
  <!-- npu="910b" id9 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id9 -->
  <!-- npu="A3" id10 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id10 -->
<!-- end id8 -->
  <!-- npu="A3,910b" id11 -->
- Host memory should be allocated using "aclrtMallocHost", which automatically aligns memory addresses. This constraint applies to:
  <!-- npu="910b" id12 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id12 -->
  <!-- npu="A3" id13 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id13 -->
<!-- end id11 -->
<!-- npu="A3" id35 -->
- Host memory in FabricMem scenarios has the following constraints: HDK 25.5 does not support `aclrtMemRetainAllocationHandle`, must use ADXL's `AdxlEngine::MallocMem` for allocation and `AdxlEngine::FreeMem` for release; HDK 26.0 and above can use ACL interfaces directly. This constraint applies to:
  <!-- npu="A3" id36 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id36 -->
<!-- end id35 -->
- Device memory should be allocated using "aclrtMalloc". If transferred via HCCS, memory allocation rule should be configured as ACL\_MEM\_MALLOC\_HUGE\_ONLY.
<!-- npu="950" id14 -->
- In Ascend 950PR/Ascend 950DT scenarios, host RoCE NIC currently does not support registering memory allocated via "aclrtMallocHost", malloc or similar methods can be used instead.
<!-- end id14 -->

## deregister\_mem

**Function**

Deregister memory. Repeated calls to deregister\_mem with the same mem\_handle will correctly release resources on the first call, and subsequent calls will return SUCCESS without performing actual operations. Passing a handle not obtained from register\_mem (non-zero) will return SUCCESS without performing actual operations; passing 0 will return PARAM\_INVALID.

**Prototype**

```python
deregister_mem(mem_handle: int) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| mem_handle | int | Memory handle returned by register\_mem interface. |

**Example**

```python
ret = engine.deregister_mem(handle)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error
- Others: Failure

**Constraints**

- Before calling this interface, disconnect must be called first to disconnect all links and ensure all memory is no longer in use.

## connect

**Function**

Establish link with remote HIXL.

**Prototype**

```python
connect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| timeout_in_millis | int | Link establishment timeout in milliseconds, default: 1000. |

**Example**

```python
ret = engine.connect("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error
- TIMEOUT: Link establishment timeout
- ALREADY\_CONNECTED: Duplicate link establishment
- Others: Failure

**Constraints**

- Must be called after both Client and Server have completed initialization via initialize interface.
  <!-- npu="A3,910b" id15 -->
- When OPTION\_LOCAL\_COMM\_RES is configured as empty, version "1.0" or "1.2", collective communication domain method is used for link establishment, maximum communication count = 512. Excessive link count may cause memory OOM and KV Cache transfer performance risks. This constraint applies to:
  <!-- npu="910b" id16 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id16 -->
  <!-- npu="A3" id17 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id17 -->
  <!-- end id15 -->
- When OPTION\_LOCAL\_COMM\_RES is configured with version "1.3" (recommended, requires HDK version >= 25.5.0 and toolkit version >= 9.1.0), HixlCS capability is used for link establishment with no link limit.
- Timeout should be configured above 200ms.
- All local and remote memory must be registered before calling this interface, otherwise registration after link establishment does not support remote access.
  <!-- npu="A3,910b" id18 -->
- In container scenarios, the "/etc/hccn.conf" file must be mapped inside the container or ensure hccn\_tool exists under the default path "/usr/local/Ascend/driver/tools". If neither can be satisfied, the user must configure the hccn\_tool path into PATH. Configuration example, where hccn\_tool\_install\_path represents the path of hccn\_tool. This constraint applies to:
  <!-- npu="910b" id19 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id19 -->
  <!-- npu="A3" id20 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id20 -->

  ```sh
  export PATH=$PATH:{hccn_tool_install_path}
  ```
  <!-- end id18 -->

- For Device RoCE scenarios, Device RoCE address configuration within the same communication cluster must be consistent. IPv6-only nodes and IPv4/IPv6 dual-stack nodes cannot be mixed. This constraint applies to:
  - Atlas A2 Training Series/Atlas A2 Inference Series
  - Atlas A3 Training Series/Atlas A3 Inference Series

## disconnect

**Function**

Disconnect link with remote HIXL.

**Prototype**

```python
disconnect(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| timeout_in_millis | int | Disconnection timeout in milliseconds, default: 1000. |

**Example**

```python
ret = engine.disconnect("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error
- NOT\_CONNECTED: No link established with remote endpoint
- Others: Failure

**Constraints**

- initialize interface must be called before this interface.

## connect\_async

**Function**

Asynchronously establish link with remote HIXL.

**Prototype**

```python
connect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| timeout_in_millis | int | Link establishment timeout in milliseconds, default: 1000. |

**Example**

```python
ret = engine.connect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error (timeout\_in\_millis <= 0)
- RESOURCE\_EXHAUSTED: Task queue is full
- Others: Failure

**Constraints**

- Inherits all constraints of connect interface.
- Thread pool thread count and task queue length are configured via Hixl's initialize interface.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- connect\_async/disconnect\_async interfaces should not be mixed with connect/disconnect interfaces.
- When multiple tasks are issued to the same remote\_engine, they are executed in submission order; tasks for different remote\_engines can be executed concurrently. The retrieved task status is the status of the most recently submitted task.

## disconnect\_async

**Function**

Asynchronously disconnect link with remote HIXL.

**Prototype**

```python
disconnect_async(remote_engine: str, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| timeout_in_millis | int | Disconnection timeout in milliseconds, default: 1000. |

**Example**

```python
ret = engine.disconnect_async("127.0.0.1:16001", timeout_in_millis=5000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error (timeout\_in\_millis <= 0)
- RESOURCE\_EXHAUSTED: Task queue is full
- Others: Failure

**Constraints**

- Inherits all constraints of disconnect interface.
- Thread pool thread count and task queue length are configured via Hixl's initialize interface.
<br>- "GlobalResourceConfig": "{"connect_pool.thread_num":"2","connect_pool.task_queue_capacity":"256"}"
- connect\_async/disconnect\_async interfaces should not be mixed with connect/disconnect interfaces.
- When multiple tasks are issued to the same remote\_engine, they are executed in submission order; tasks for different remote\_engines can be executed concurrently. The retrieved task status is the status of the most recently submitted task.

## get\_async\_connect\_status

**Function**

Get specified asynchronous connection status.

**Prototype**

```python
get_async_connect_status(remote_engine: str) -> Tuple[int, AsyncConnectStatus]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |

**Example**

```python
ret, status = engine.get_async_connect_status("127.0.0.1:16001")
if status == hixl.AsyncConnectStatus.CONNECTED:
    print("Connected")
```

**Return Value**

Returns a tuple (ret, status):

- ret: Status code. SUCCESS indicates success, others indicate failure.
- status: Asynchronous connection status, enum values see [AsyncConnectStatus](HIXL-data-structure.md#asyncconnectstatus).

**Constraints**

- initialize interface must be called before this interface.
- The return value only indicates whether the interface call succeeded. Asynchronous link establishment/disconnection task status is represented by the output parameter.

## get\_all\_async\_connect\_status

**Function**

Get all asynchronous connection statuses.

**Prototype**

```python
get_all_async_connect_status() -> Tuple[int, Dict[str, AsyncConnectStatus]]
```

**Parameters**

None

**Example**

```python
ret, statuses = engine.get_all_async_connect_status()
for remote, status in statuses.items():
    print(f"{remote}: {status}")
```

**Return Value**

Returns a tuple (ret, statuses):

- ret: Status code. SUCCESS indicates success, others indicate failure.
- statuses: Dictionary where key is remote HIXL identifier (str) and value is asynchronous connection status (AsyncConnectStatus).

**Constraints**

- initialize interface must be called before this interface.
- The return value only indicates whether the interface call succeeded. Asynchronous link establishment/disconnection task status is represented by the output parameter.

## transfer\_sync

**Function**

Perform memory transfer with remote HIXL.

**Prototype**

```python
transfer_sync(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| op | TransferOp | Read from remote memory to local or write from local memory to remote. |
| op_descs | List[TransferOpDesc] | Batch operation local and remote addresses. |
| timeout_in_millis | int | Transfer timeout in milliseconds, default: 1000. |

**Example**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret = engine.transfer_sync("127.0.0.1:16001", hixl.TransferOp.READ, op_descs, timeout_in_millis=30000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error
- NOT\_CONNECTED: No link established with remote endpoint
- TIMEOUT: Transfer timeout
- RESOURCE\_EXHAUSTED: Resource exhausted
- Others: Failure

**Constraints**

  <!-- npu="A3,910b" id21 -->
- Before calling this interface, connect interface must be called to establish link with remote endpoint, or link pool mechanism must be enabled during HIXL initialization (by configuring OPTION\_GLOBAL\_RESOURCE\_CONFIG parameter in options). This constraint applies to:
  <!-- npu="910b" id22 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id22 -->
  <!-- npu="A3" id23 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id23 -->
  <!-- end id21 -->
  <!-- npu="A3,910b" id24 -->
- The system enables relay memory pool by default. When relay memory pool is enabled, if either local or remote memory in op\_desc is not registered, it will be determined to use relay transfer mode, and unregistered memory is treated as Host memory. Users must ensure address validity. This constraint applies to:
  <!-- npu="910b" id25 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id25 -->
  <!-- npu="A3" id26 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id26 -->
  <!-- end id24 -->
  <!-- npu="A3,910b" id27 -->
- In relay transfer mode, all op\_desc transfer types must be the same. For example, all op\_descs are local Host memory writing to remote Host memory. This constraint applies to:
  <!-- npu="910b" id28 -->
  - Atlas A2 Training Series/Atlas A2 Inference Series
  <!-- end id28 -->
  <!-- npu="A3" id29 -->
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id29 -->
  <!-- end id27 -->
  <!-- npu="A3" id30 -->
- In Fabric Mem transfer mode, all op\_descs transfer types must be the same. The system determines transfer direction based on the memory type of the first op\_desc. This constraint applies to:
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id30 -->

## transfer\_async

**Function**

Perform batch asynchronous memory transfer with remote HIXL.

**Prototype**

```python
transfer_async(remote_engine: str, op: TransferOp, op_descs: List[TransferOpDesc], args: TransferArgs = TransferArgs()) -> Tuple[int, int]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote HIXL. |
| op | TransferOp | Read from remote memory to local or write from local memory to remote. |
| op_descs | List[TransferOpDesc] | Batch operation local and remote addresses. |
| args | TransferArgs | Optional parameters (reserved). |

**Example**

```python
op_descs = [hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=size)]
ret, req_id = engine.transfer_async("127.0.0.1:16001", hixl.TransferOp.WRITE, op_descs)
```

**Return Value**

Returns a tuple (ret, req\_id):

- ret: Status code. SUCCESS indicates success, NOT\_CONNECTED indicates no link established with remote endpoint, RESOURCE\_EXHAUSTED indicates resource exhausted, others indicate failure.
- req\_id: Request handle (int type), used to query transfer request status.

**Constraints**

- Before calling this interface, the following constraints exist:
  - connect interface must be called first to establish link with remote endpoint.
  <!-- npu="A3,910b" id31 -->
  - Or link pool mechanism must be enabled during HIXL initialization (by configuring OPTION\_GLOBAL\_RESOURCE\_CONFIG parameter in options). This constraint applies to:
    <!-- npu="910b" id32 -->
    - Atlas A2 Training Series/Atlas A2 Inference Series
    <!-- end id32 -->
    <!-- npu="A3" id33 -->
    - Atlas A3 Training Series/Atlas A3 Inference Series
    <!-- end id33 -->
  <!-- end id31 -->
- Currently asynchronous transfer only supports direct transfer, relay transfer is not supported yet, default is direct transfer.
  <!-- npu="A3" id34 -->
- In Fabric Mem transfer mode, all op\_descs transfer types must be the same. The system determines transfer direction based on the memory type of the first op\_desc. This constraint applies to:
  - Atlas A3 Training Series/Atlas A3 Inference Series
  <!-- end id34 -->

## get\_transfer\_status

**Function**

Get asynchronous memory transfer status.

**Prototype**

```python
get_transfer_status(req_id: int) -> Tuple[int, TransferStatus]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| req_id | int | Request handle, generated by calling transfer\_async. |

**Example**

```python
ret, status = engine.get_transfer_status(req_id)
if status == hixl.TransferStatus.COMPLETED:
    print("Transfer completed")
```

**Return Value**

Returns a tuple (ret, status):

- ret: Status code. SUCCESS indicates success, PARAM\_INVALID indicates parameter error, NOT\_CONNECTED indicates no link established with remote endpoint, others indicate failure.
- status: Transfer status, enum values see [TransferStatus](HIXL-data-structure.md#transferstatus).

**Constraints**

- Before calling this interface, connect interface must be called to establish link with remote endpoint.
- After calling transfer\_async interface for asynchronous transfer, this interface must be used to query corresponding request status. If the queried status is COMPLETED or FAILED, related resources will be released. In this scenario, querying again is not supported.
- During asynchronous transfer, users should judge timeout themselves. If users determine the task has timed out, disconnect interface must be called to destroy the link and clean up related resources.

## get\_all\_transfer\_status

**Function**

Get all asynchronous memory transfer statuses.

**Prototype**

```python
get_all_transfer_status(args: GetTransferStatusArgs = GetTransferStatusArgs()) -> Tuple[int, List[TransferResult]]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| args | GetTransferStatusArgs | Parameters for getting all asynchronous transfer request statuses. |

**Example**

```python
args = hixl.GetTransferStatusArgs(max_query_count=4, skip_waiting=True)
ret, results = engine.get_all_transfer_status(args)
for result in results:
    print(f"req={result.req}, status={result.status}")
```

**Return Value**

Returns a tuple (ret, results):

- ret: Status code. SUCCESS indicates success, UNSUPPORTED indicates that Hixl initialization options did not configure LocalCommRes version as 1.3 and did not configure GlobalResourceConfig comm\_resource\_config.protocol\_desc to include uboe:device or ub\_rtp:device, others indicate failure.
- results: TransferResult list, each element contains transfer request status information.

**Constraints**

- Before calling this interface, connect interface must be called to establish link with remote endpoint.
- After calling transfer\_async interface for asynchronous transfer, this interface must be used to query all request statuses. If a request status is COMPLETED or FAILED, related resources will be released. In this scenario, querying again will not return that request status.
- During asynchronous transfer, users should judge timeout themselves. If users determine the task has timed out, it is recommended to call disconnect interface to destroy the link and clean up related resources.

## send\_notify

**Function**

Send Notify information to remote engine.

**Prototype**

```python
send_notify(remote_engine: str, notify: NotifyDesc, timeout_in_millis: int = 1000) -> int
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| remote_engine | str | Unique identifier of remote Hixl. |
| notify | NotifyDesc | Notify content to send. The length limit for notify\_msg and name in the content is 1024 characters. |
| timeout_in_millis | int | Send timeout in milliseconds, default: 1000. |

**Example**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
ret = engine.send_notify("127.0.0.1:16001", notify, timeout_in_millis=1000)
```

**Return Value**

- SUCCESS: Success
- PARAM\_INVALID: Parameter error (timeout\_in\_millis <= 0, or notify.name/notify\_msg length exceeds 1024)
- Others: Failure

**Constraints**

- Before calling this interface, connect interface must be called to establish link with remote endpoint.
- Each link can have at most 4096 Notify messages. Remote Hixl must call get\_notifies interface in time to consume Notify messages to prevent triggering the limit and causing send failures.

## get\_notifies

**Function**

Get all Notify messages received by all Servers in the current Hixl, and clear received messages.

**Prototype**

```python
get_notifies() -> Tuple[int, List[NotifyDesc]]
```

**Parameters**

None

**Example**

```python
ret, notifies = engine.get_notifies()
for n in notifies:
    print(f"name={n.name}, msg={n.notify_msg}")
```

**Return Value**

Returns a tuple (ret, notifies):

- ret: Status code. SUCCESS indicates success, others indicate failure.
- notifies: NotifyDesc list.

**Constraints**

None

## get\_capability

**Function**

Query library capability features. Upper layer can call this interface before initialize to detect whether the current Hixl library supports specific capabilities (such as Auto Connect, Client/Server communication), avoiding hard-coded defaults or incompatibility with older .so versions.

**Prototype**

```python
hixl.get_capability(feature_type: FeatureType) -> Tuple[int, int]
```

**Parameters**

| Parameter | Type | Description |
| --- | --- | --- |
| feature_type | FeatureType | Feature type, values see [FeatureType](HIXL-data-structure.md#featuretype). |

**Example**

```python
ret, value = hixl.get_capability(hixl.FeatureType.AUTO_CONNECT)
supports_auto_connect = (value == hixl.FEATURE_SUPPORTED)
```

**Return Value**

Returns a tuple (ret, value):

- ret: Status code. SUCCESS indicates success. For unknown or unsupported feature types, value is FEATURE\_NOT\_SUPPORTED. PARAM\_INVALID indicates invalid parameter (feature\_type is negative).
- value: Feature support status. 1 indicates supported (FEATURE\_SUPPORTED), 0 indicates not supported (FEATURE\_NOT\_SUPPORTED).

**Constraints**

- Module-level function, does not depend on Hixl instance.
