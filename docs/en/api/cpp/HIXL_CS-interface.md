
# HIXL_CS APIs<a id="hixl-cs-apis"></a>

<!-- md-trans-meta sourceCommit=53e9e702fa1264d5a079c2b9f93d0655b9d4c290 translatedAt=2026-11-19T01:01:42.795Z pushedAt=2026-11-19T01:04:51.451Z nl=2 -->

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

## HixlStatus and Return Codes

API return code type and constants.

```cpp
typedef uint32_t HixlStatus;
static const uint32_t HIXL_SUCCESS = 0U;
static const uint32_t HIXL_PARAM_INVALID = 103900U;
static const uint32_t HIXL_TIMEOUT = 103901U;
static const uint32_t HIXL_FAILED = 503900U;
static const uint32_t HIXL_RESOURCE_EXHAUSTED = 203900U;
```

The error codes are described below.

| Enum Value | Meaning | Recoverable | Solution |
| --- | --- | --- | --- |
| HIXL_SUCCESS | Success | None | Not applicable. |
| HIXL_PARAM_INVALID | Invalid parameter | Yes | Troubleshoot the cause using the logs. |
| HIXL_TIMEOUT | Processing timed out | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| HIXL_FAILED | General failure | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| HIXL_RESOURCE_EXHAUSTED | Resources exhausted (for example, the limit on transfer task slots or concurrent TLS handshakes has been reached) | Yes | Use HixlCSClientQueryCompleteStatus to check in-flight transfer task status and release slots before retrying. If TLS handshakes are limited, reduce connection establishment concurrency. |

## Handle Types

Handle type definitions.

```cpp
typedef void *HixlServerHandle;
typedef void *HixlClientHandle;
typedef void *CompleteHandle;
typedef void *MemHandle;
```

## Structures

The following structures are used in the CS APIs.

### HixlServerConfig

Server configuration.

```cpp
struct HixlServerConfig {
  const char *global_resource_config = nullptr;
  uint8_t reserved[120] = {};
};
```

Fields:

| Field | Type | Description |
|---|---|---|
| global_resource_config | const char* | Global resource configuration as a JSON string. Currently configures Server communication resource parameters. **NULL** or an empty string means not configured. |
| reserved | uint8_t[120] | Reserved **HixlServerConfig** field for future extension, keeping the structure size at 128 bytes. |

### HixlClientConfig

Client configuration.

```cpp
struct HixlClientConfig {
  const char *global_resource_config = nullptr;
  uint8_t reserved[120] = {};
};
```

Fields:

| Field | Type | Description |
|---|---|---|
| global_resource_config | const char* | Global resource configuration as a JSON string. Currently configures Client communication resource parameters. **NULL** or an empty string means not configured. |
| reserved | uint8_t[120] | Reserved **HixlClientConfig** field for future extension, keeping the structure size at 128 bytes. |

### global_resource_config Configuration

All HIXL CS objects in the same process must use the same control plane TLS configuration, including whether TLS is enabled. Otherwise, object creation returns a parameter error. To change the configuration, first destroy all related objects in the process, and then recreate them. These requirements also apply when HIXL CS is used together with HIXL, ADXL, or LLM-DataDist. TLS is disabled if no `tls_config.*` fields are configured.

Server `global_resource_config` currently supports the following settings.

| Setting | Type | Required | Description |
|---|---|---|---|
| comm_resource_config.listen_port | Integer | Optional | Server communication resource listening port for CS connection establishment, in [1, 65535]. When configured, the Server returns this port in its response during Client Endpoint matching. Otherwise, the Server retains automatic listening port discovery. An out-of-range value causes Server creation to fail with an invalid parameter error. |
| comm_resource_config.max_active_channels | Integer | Optional | Concurrently active Server device transfer channels, in [1, 8192], default 128. Each active channel consumes 2 Stream resources. Leave headroom based on the card's Stream limit and Streams already created by your application. For card-specific limits, see CANN Runtime API [aclrtCreateStream](https://www.hiascend.com/document/detail/en/canncommercial/latest/API/runtimeapi/aclcppdevg_03_0066.html). Values below 1, above 8192, or nonnumeric values cause Server creation to fail with an invalid parameter error. |
| fabric_memory.max_capacity | Integer | Optional | UB_MEM virtual memory pool size, in TB, range (0, 1024], default 32. Applies only when the created endpoint uses UB_MEM. Configure before the process creates its first UB_MEM Server or Client. An out-of-range value causes creation to fail with an invalid parameter error. |
| fabric_memory.start_address | Integer | Optional | UB_MEM virtual memory pool starting address, in TB, range [0, 1024], default 40. Applies only when the created endpoint uses UB_MEM. Configure before the process creates its first UB_MEM Server or Client. An out-of-range value causes creation to fail with an invalid parameter error. |
| fabric_memory.task_stream_num | Integer | Optional | Streams per task, in [1, 8]. CS parses and saves this field. An out-of-range value causes creation to fail with an invalid parameter error. When supplied through HIXL Engine with `fabric_memory.enable_aicpu_unfold` set to **true**, only 1 is supported. |
| fabric_memory.enable_aicpu_unfold | Boolean | Optional | Whether AI CPU unfolds UB_MEM. Default: **true**. **true** uses Device AI CPU to unfold SDMA; **false** submits `aclrtMemcpyAsync` calls individually from the Host onto the Device stream. A non-Boolean value causes creation to fail with an invalid parameter error. |
| tls_config.cert_file | String | Required when TLS is enabled | Path to the control plane TCP TLS certificate file. It must be configured together with `tls_config.key_file`. Otherwise, creation fails with a parameter error without falling back to plaintext. |
| tls_config.key_file | String | Required when TLS is enabled | Path to the control plane TCP TLS private key file. It must be configured together with `tls_config.cert_file`. Otherwise, creation fails with a parameter error without falling back to plaintext. |
| tls_config.ca_file | String | Optional | Path to the CA file used to verify the peer certificate. Required when verify_peer is true. |
| tls_config.verify_peer | Boolean | Optional | Whether to require and verify a peer certificate. Defaults to false. The control plane establishes connections by IP address and does not perform hostname/SNI verification. |
| tls_config.ciphers | String | Optional | OpenSSL cipher suites; applies only to TLS 1.2. For example, `ECDHE-RSA-AES256-GCM-SHA384`. An invalid value causes creation to return a parameter error. |
| tls_config.enable_cert_refresh | Boolean | Optional | Whether to automatically load changed certificate files. Defaults to false. When enabled, a successfully loaded new certificate applies only to connections established afterward; existing connections are unaffected. If loading fails, the original certificate remains in use. When disabled, changes to certificate files do not take effect automatically. |

Client `global_resource_config` currently supports the following settings.

| Setting | Type | Required | Description |
|---|---|---|---|
| comm_resource_config.qos | Number | Optional | Communication protocol QoS. Currently supports only [0-7]. |
| comm_resource_config.max_active_channels | Integer | Optional | Concurrently active Client device transfer channels, in [1, 8192], default 128. Each active channel consumes 2 Stream resources. Leave headroom based on the card's Stream limit and Streams already created by your application. For card-specific limits, see CANN Runtime API [aclrtCreateStream](https://www.hiascend.com/document/detail/en/canncommercial/latest/API/runtimeapi/aclcppdevg_03_0066.html). Values below 1, above 8192, or nonnumeric values cause Client creation to fail with an invalid parameter error. |
| transfer_config.max_transfer_count_per_batch | Number or decimal digit string | Optional | Maximum number of buffers per internal transfer batch. If this limit is exceeded, buffers are automatically split into batches in their original order. Default: 1920. Global range: [1, 32766]; HCCS and UB_MEM range: [1, 1920]. Supported in CANN 9.2.0 and later. |
| fabric_memory.max_capacity | Integer | Optional | UB_MEM virtual memory pool size, in TB, range (0, 1024], default 32. Applies only when the created endpoint uses UB_MEM. Configure before the process creates its first UB_MEM Server or Client. An out-of-range value causes creation to fail with an invalid parameter error. |
| fabric_memory.start_address | Integer | Optional | UB_MEM virtual memory pool starting address, in TB, range [0, 1024], default 40. Applies only when the created endpoint uses UB_MEM. Configure before the process creates its first UB_MEM Server or Client. An out-of-range value causes creation to fail with an invalid parameter error. |
| fabric_memory.task_stream_num | Integer | Optional | Streams per task, in [1, 8]. CS parses and saves this field. An out-of-range value causes creation to fail with an invalid parameter error. When supplied through HIXL Engine with `fabric_memory.enable_aicpu_unfold` set to **true**, only 1 is supported. |
| fabric_memory.enable_aicpu_unfold | Boolean | Optional | Whether AI CPU unfolds UB_MEM. Default: **true**. **true** uses Device AI CPU to unfold SDMA; **false** submits `aclrtMemcpyAsync` calls individually from the Host onto the Device stream. A non-Boolean value causes creation to fail with an invalid parameter error. |
| tls_config.cert_file | String | Required when TLS is enabled | Path to the control plane TCP TLS certificate file. It must be configured together with `tls_config.key_file`. Otherwise, creation fails with a parameter error without falling back to plaintext. |
| tls_config.key_file | String | Required when TLS is enabled | Path to the control plane TCP TLS private key file. It must be configured together with `tls_config.cert_file`. Otherwise, creation fails with a parameter error without falling back to plaintext. |
| tls_config.ca_file | String | Optional | Path to the CA file used to verify the peer certificate. Required when verify_peer is true. |
| tls_config.verify_peer | Boolean | Optional | Whether to require and verify a peer certificate. Defaults to false. The control plane establishes connections by IP address and does not perform hostname/SNI verification. |
| tls_config.ciphers | String | Optional | OpenSSL cipher suites; applies only to TLS 1.2. For example, `ECDHE-RSA-AES256-GCM-SHA384`. An invalid value causes creation to return a parameter error. |
| tls_config.enable_cert_refresh | Boolean | Optional | Whether to automatically load changed certificate files. Defaults to false. When enabled, a successfully loaded new certificate applies only to connections established afterward; existing connections are unaffected. If loading fails, the original certificate remains in use. When disabled, changes to certificate files do not take effect automatically. |

Server configuration example:

```json
{
  "comm_resource_config.listen_port": 26666
}
```

Client configuration example:

```json
{
  "comm_resource_config.qos": 7,
  "transfer_config.max_transfer_count_per_batch": 1920
}
```

UB_MEM configuration example (usable by both Server and Client; supported only on Atlas A3 and Ascend 950PR&950DT products):

```json
{
  "fabric_memory.max_capacity": 32,
  "fabric_memory.start_address": 40,
  "fabric_memory.task_stream_num": 1,
  "fabric_memory.enable_aicpu_unfold": true
}
```

### HixlClientDesc

Client description.

```cpp
struct HixlClientDesc {
  const EndpointDesc *local_endpoint;
  const EndpointDesc *remote_endpoint;
  const char *server_ip;
  uint32_t server_port;
  uint8_t tc;
  uint8_t sl;
  uint8_t reserved[98] = {};
};
```

Fields:

| Field | Type | Description |
|---|---|---|
| local_endpoint | const EndpointDesc* | Local Endpoint description (defined in <https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h>).|
| remote_endpoint | const EndpointDesc* | Remote Endpoint description (defined in <https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h>).|
| server_ip | const char* | Target server Host IP address.|
| server_port | uint32_t | Target server Host port, in [1, 65535].|
| tc | uint8_t | RDMA NIC traffic class.|
| sl | uint8_t | RDMA NIC service level.|
| reserved | uint8_t[98] | Reserved **HixlClientDesc** field for future extension, keeping the structure size at 128 bytes.|

### HixlServerDesc

Server description.

```cpp
struct HixlServerDesc {
  const EndpointDesc *endpoint_list;
  const char *server_ip;
  uint32_t server_port;
  uint32_t endpoint_list_num;
  uint8_t reserved[104] = {};
};
```

Fields:

| Field | Type | Description |
|---|---|---|
| server_ip | const char* | Listening IP address.|
| server_port | uint32_t | Listening port, in [0, 65535]. **0** disables listening.|
| endpoint_list | const EndpointDesc* | Pointer to an Endpoint description array. For the definition, see <https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h>.|
| endpoint_list_num | uint32_t | Number of Endpoints.|
| reserved | uint8_t[104] | Reserved **HixlServerDesc** field for future extension, keeping the structure size at 128 bytes.|

### HixlOneSideOpDesc

One-sided operation description for batch put/get.

```cpp
struct HixlOneSideOpDesc {
  void *remote_buf;
  void *local_buf;
  uint64_t len;
};
```

Fields:

| Field | Type | Description |
|---|---|---|
| remote_buf | void* | Remote (server) data address.|
| local_buf | void* | Local (client) data address.|
| len | uint64_t | Transfer length in bytes.|

Put and Get semantics for `HixlOneSideOpDesc`:

- **BatchPut (Synchronous/Asynchronous)**: Copies `len` bytes from `local_buf` to `remote_buf`.
- **BatchGet (Synchronous/Asynchronous)**: Copies `len` bytes from `remote_buf` to `local_buf`.

## Enums

Asynchronous task completion status:

```cpp
enum HixlCompleteStatus {
  HIXL_COMPLETE_STATUS_WAITING,
  HIXL_COMPLETE_STATUS_COMPLETED,
  HIXL_COMPLETE_STATUS_TIMEOUT,
  HIXL_COMPLETE_STATUS_FAILED
};

```

Note:
**HIXL_COMPLETE_STATUS_TIMEOUT** is currently reserved. **HIXL_COMPLETE_STATUS_FAILED** is returned when a transfer task fails. Querying this status automatically releases related resources; querying again with the same **complete_handle** is unsupported.

## APIs

Each API includes a description, prototype, parameters, return value, example, and constraints.

### HixlCSServerCreate

**Description**

Creates and initializes a Server instance.

**Prototype**

```cpp
HixlStatus HixlCSServerCreate(const HixlServerDesc *server_desc,
                              const HixlServerConfig *config,
                              HixlServerHandle *server_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| server_desc | Input | Server description containing the listening IP/port and endpoint list. |
| config | Input | Server configuration; **NULL** is unsupported. To configure the communication resource listening port, set `HixlServerConfig.global_resource_config`. |
| server_handle | Output | Created **HixlServerHandle**. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure or error code (see constants).

**Constraints**

- If using a local Device Endpoint, call **aclrtSetDevice** before this API.
- **server_handle** is not guaranteed to be valid if the call fails.

### HixlCSServerRegMem

**Description**

The Server registers shared memory for Client access.

**Prototype**

```cpp
HixlStatus HixlCSServerRegMem(HixlServerHandle server_handle,
                              const char *mem_tag,
                              const CommMem *mem,
                              MemHandle *mem_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| server_handle | Input | Server instance handle. |
| mem_tag | Input | Optional memory tag string identifying this memory. Can be **NULL**. |
| mem | Input | Pointer to **CommMem**, describing memory location and size (defined in <https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h>). |
| mem_handle | Output | Returned memory handle for subsequent unregistration. |

**Return Value**

- **HIXL_SUCCESS**: Registration succeeded.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Register all local memory before the client calls **HixlCSClientConnect** to connect to this Server.
- Host RoCE NICs currently do not support registering memory allocated by **aclrtMallocHost**. Use **malloc** or another method.
- Device memory to be registered must be allocated using **aclrtMalloc**.

### HixlCSServerListen

**Description**

Starts Server listening to accept Client connections.

**Prototype**

```cpp
HixlStatus HixlCSServerListen(HixlServerHandle server_handle, uint32_t backlog);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| server_handle | Input | Server instance handle. |
| backlog | Input | Maximum connection queue length. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

Start listening before the client calls **HixlCSClientConnect** to connect to this Server.
If the same Server is already listening, calling this API again fails; the existing listener remains available.

### HixlCSServerUnregMem

**Description**

Unregisters previously registered memory.

**Prototype**

```cpp
HixlStatus HixlCSServerUnregMem(HixlServerHandle server_handle, MemHandle mem_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| server_handle | Input | Server instance handle. |
| mem_handle | Input | Memory handle to unregister, returned by **HixlCSServerRegMem**. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

Unregister only after remote access has stopped and all clients connected to the current server have disconnected.

### HixlCSServerDestroy

**Description**

Destroys a Server instance and releases related resources.

**Prototype**

```cpp
HixlStatus HixlCSServerDestroy(HixlServerHandle server_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| server_handle | Input | Server instance handle. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Use this API together with **HixlCSServerCreate**.
- It is recommended that you disconnect connections and unregister memory before calling **HixlCSServerDestroy**.
- The Server must call this API after all Clients disconnect. If the Server exits earlier, errors occur during Client disconnection and data transfer.
- If a Client reads or writes remote Server addresses, the Server must wait until the Client finishes these operations before calling this API. Otherwise, the operations fail.
- This API cannot be called concurrently with other APIs.

### HixlCSClientCreate

**Description**

Creates a Client instance and initializes local resources.

**Prototype**

```cpp
HixlStatus HixlCSClientCreate(const HixlClientDesc *client_desc,
                              const HixlClientConfig *config,
                              HixlClientHandle *client_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_desc | Input | Client description containing the server IP/port and endpoints. |
| config | Input | Client configuration; **NULL** is unsupported. To configure communication resource parameters, set `HixlClientConfig.global_resource_config`. |
| client_handle | Output | Created client handle. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- If using a local Device Endpoint, call **aclrtSetDevice** before this API.
- `client_handle` is not guaranteed to be valid if the call fails.

### HixlCSClientConnect

**Description**

Synchronously connects to a Server, blocking until success or timeout.

**Prototype**

```cpp
HixlStatus HixlCSClientConnect(HixlClientHandle client_handle, uint32_t timeout_ms);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| timeout_ms | Input | Connection timeout in milliseconds. |

**Return Value**

- **HIXL_SUCCESS**: Connection established.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- **HIXL_TIMEOUT**: Connection timed out.
- Other values: Failure.

**Constraints**

- Register all local memory before calling this API to connect to the server.
- Ensure that the server is listening before calling this API to connect.
- This API automatically prefetches remote memory descriptors during connection establishment. Call **HixlCSClientGetRemoteMem** to retrieve or refresh them later.

### HixlCSClientGetRemoteMem

**Description**

Obtains memory information registered on the Server.

**Prototype**

```cpp
HixlStatus HixlCSClientGetRemoteMem(HixlClientHandle client_handle,
                                    CommMem **remote_mem_list,
                                    char ***mem_tag_list,
                                    uint32_t *list_num,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| remote_mem_list | Output | Returned `CommMem` array pointer. Its lifetime is managed internally. Repeated calls release the previous allocation; copy it to local memory if needed (defined in <https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h>). |
| mem_tag_list | Output | String array of tags for each `CommMem`. Its lifetime is managed internally. Repeated calls release the previous allocation; copy it to local memory if needed. |
| list_num | Output | Returned list length. Its lifetime is managed internally. Repeated calls release the previous allocation; copy it to local memory if needed. |
| timeout_ms | Input | Request timeout in milliseconds. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure or timeout.

**Constraints**

- Output parameter memory lifetime is managed internally. Repeated calls release the previous allocation; copy it to local memory if needed.
- **HixlCSClientConnect** automatically prefetches remote memory descriptors. Call this API to retrieve or refresh them later.

### HixlCSClientRegMem

**Description**

The Client registers local memory.

**Prototype**

```cpp
HixlStatus HixlCSClientRegMem(HixlClientHandle client_handle,
                              const char *mem_tag,
                              const CommMem *mem,
                              MemHandle *mem_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| mem_tag | Input | Memory tag string. |
| mem | Input | `CommMem` description. |
| mem_handle | Output | Returned memory handle. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Register all local memory before calling **HixlCSClientConnect** to connect to the server.
- Host RoCE NICs currently do not support registering memory allocated by **aclrtMallocHost**. Use **malloc** or another method.
- Device memory to be registered must be allocated using **aclrtMalloc**.

### HixlCSClientUnregMem

**Description**

Unregisters memory registered by the Client.

**Prototype**

```cpp
HixlStatus HixlCSClientUnregMem(HixlClientHandle client_handle, MemHandle mem_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| mem_handle | Input | Memory handle to unregister. |

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

Unregister only after local access has stopped and connections to the server have been disconnected.

### HixlCSClientBatchPutAsync

**Description**

Asynchronously writes multiple data groups to the Server in a batch.

**Prototype**

```cpp
HixlStatus HixlCSClientBatchPutAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| list_num | Input | Subtask count, greater than 0. **0** returns `HIXL_PARAM_INVALID`. |
| desc_list | Input | Pointer to a `HixlOneSideOpDesc` array of length `list_num`. Cannot be **NULL** when `list_num > 0`. Each entry transfers **local_buf → remote_buf**, with `len` in bytes. |
| complete_handle | Output | Returned completion handle for asynchronous task status queries. Related resources are released after a successful query. Cannot be **NULL**. |

**Return Value**

- **HIXL_SUCCESS**: Task submitted (not necessarily completed).
- **HIXL_PARAM_INVALID**: Invalid parameter.
- **HIXL_RESOURCE_EXHAUSTED**: Concurrent task resources exhausted. Call **HixlCSClientQueryCompleteStatus** to release completed tasks, then retry.
- Other values: Submission failed.

**Constraints**

- Up to 4K concurrent transfer tasks may await completion queries (this is not the per-submission **list_num** limit). Query task statuses promptly through **HixlCSClientQueryCompleteStatus** after submission to release resources.
- If any batch transfer returns `HIXL_FAILED` or `HIXL_TIMEOUT`, the client enters a failed state, and subsequent batch transfer APIs return `HIXL_FAILED` immediately. Recreate the client to continue.

### HixlCSClientBatchGetAsync

**Description**

Asynchronously reads multiple data groups from the Server into local memory in a batch.

**Prototype**

```cpp
HixlStatus HixlCSClientBatchGetAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| list_num | Input | Subtask count, greater than 0. **0** returns `HIXL_PARAM_INVALID`. |
| desc_list | Input | Pointer to a `HixlOneSideOpDesc` array of length `list_num`. Cannot be **NULL** when `list_num > 0`. Each entry transfers **remote_buf → local_buf**, with `len` in bytes. |
| complete_handle | Output | Returned completion handle for asynchronous task status queries. Related resources are released after a successful query. Cannot be **NULL**. |

**Return Value**

- **HIXL_SUCCESS**: Task submitted (not necessarily completed).
- **HIXL_PARAM_INVALID**: Invalid parameter.
- **HIXL_RESOURCE_EXHAUSTED**: Concurrent task resources exhausted. Call **HixlCSClientQueryCompleteStatus** to release completed tasks, then retry.
- Other values: Submission failed.

**Constraints**

- Up to 4K concurrent transfer tasks may await completion queries (this is not the per-submission **list_num** limit). Query task statuses promptly through **HixlCSClientQueryCompleteStatus** after submission to release resources.
- If any batch transfer returns `HIXL_FAILED` or `HIXL_TIMEOUT`, the client enters a failed state, and subsequent batch transfer APIs return `HIXL_FAILED` immediately. Recreate the client to continue.

### HixlCSClientBatchPutSync

**Description**

Synchronously writes multiple data groups to the Server in a batch, blocking the current thread until the entire batch completes, times out, or fails. Does not return a `CompleteHandle`.

**Prototype**

```cpp
HixlStatus HixlCSClientBatchPutSync(HixlClientHandle client_handle,
                                    uint32_t list_num,
                                    const HixlOneSideOpDesc *desc_list,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. Cannot be **NULL**. |
| list_num | Input | Subtask count, greater than 0. **0** returns `HIXL_PARAM_INVALID`. |
| desc_list | Input | `HixlOneSideOpDesc` array of length `list_num`. Cannot be **NULL** when `list_num > 0`. Direction: **local_buf (source) → remote_buf (destination)**. `len` is the byte count per transfer. |
| timeout_ms | Input | Total wait timeout for the batch, in milliseconds. Timeout returns `HIXL_TIMEOUT`. The Host implementation polls for completion; the Device implementation uses a stream synchronization timeout budget. |

**Return Value**

- **HIXL_SUCCESS**: Entire batch completed.
- **HIXL_PARAM_INVALID**: Invalid parameter, such as `list_num == 0`, invalid `desc_list`, or **NULL** `client_handle`.
- **HIXL_TIMEOUT**: Not completed within `timeout_ms`.
- Other values: Transfer or internal error (see logs and `HixlStatus` codes).

**Constraints**

- Establish a connection to the Server first (for example, through `HixlCSClientConnect`). Addresses in `desc_list` must be valid one-sided access addresses registered or obtained through APIs such as `HixlCSClientGetRemoteMem`.
- As with asynchronous APIs, observe limits such as the maximum concurrent subtask count (see the asynchronous batch API descriptions).

### HixlCSClientBatchGetSync

**Description**

Synchronously reads multiple data groups from the Server into local memory in a batch, blocking the current thread until the entire batch completes, times out, or fails. Does not return a `CompleteHandle`.

**Prototype**

```cpp
HixlStatus HixlCSClientBatchGetSync(HixlClientHandle client_handle,
                                    uint32_t list_num,
                                    const HixlOneSideOpDesc *desc_list,
                                    uint32_t timeout_ms);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. Cannot be **NULL**. |
| list_num | Input | Subtask count, greater than 0. **0** returns `HIXL_PARAM_INVALID`. |
| desc_list | Input | `HixlOneSideOpDesc` array of length `list_num`. Cannot be **NULL** when `list_num > 0`. Direction: **remote_buf (source) → local_buf (destination)**. `len` is the byte count per transfer. |
| timeout_ms | Input | Total wait timeout for the batch, in milliseconds. Timeout returns `HIXL_TIMEOUT`. The Host implementation polls for completion; the Device implementation uses a stream synchronization timeout budget. |

**Return Value**

- **HIXL_SUCCESS**: Entire batch completed.
- **HIXL_PARAM_INVALID**: Invalid parameter, such as `list_num == 0`, invalid `desc_list`, or **NULL** `client_handle`.
- **HIXL_TIMEOUT**: Not completed within `timeout_ms`.
- Other values: Transfer or internal error (see logs and `HixlStatus` codes).

**Constraints**

- Establish a connection to the Server first (for example, through `HixlCSClientConnect`). Addresses in `desc_list` must be valid one-sided access addresses registered or obtained through APIs such as `HixlCSClientGetRemoteMem`.
- As with asynchronous APIs, observe limits such as the maximum concurrent subtask count (see the asynchronous batch API descriptions).

### HixlCSClientQueryCompleteStatus

**Description**

Queries the completion status of an asynchronous batch task.

**Prototype**

```cpp
HixlStatus HixlCSClientQueryCompleteStatus(HixlClientHandle client_handle,
                                           CompleteHandle complete_handle,
                                           HixlCompleteStatus *complete_status);
```

**Parameters**

| Parameter | Input/Output | Description |
| --- | --- | --- |
| client_handle | Input | Client handle. |
| complete_handle | Input | Task handle to query. |
| complete_status | Output | Returned task status enum (see `HixlCompleteStatus`). |

**Return Value**

- **HIXL_SUCCESS**: Query succeeded. Determine the transfer status from **complete_status**.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Up to 4K concurrent transfer tasks may await completion queries (this is not the per-submission **list_num** limit). Query task statuses promptly through **HixlCSClientQueryCompleteStatus** after submission to release resources.
- Querying **HIXL_COMPLETE_STATUS_COMPLETED** or **HIXL_COMPLETE_STATUS_FAILED** automatically releases related resources. Querying again with the same **complete_handle** is unsupported.
- If the queried status is **HIXL_COMPLETE_STATUS_WAITING**, the user must determine whether the current transfer task has timed out. If so, establish a new connection to retry and destroy the abnormal connection.

### HixlCSClientDestroy

**Description**

Destroys a Client instance and releases resources.

**Prototype**

```cpp
HixlStatus HixlCSClientDestroy(HixlClientHandle client_handle);
```

**Parameters**

| Parameter | Input/Output | Description |
|---|---:|---|
| client_handle | Input | Client handle.|

**Return Value**

- **HIXL_SUCCESS**: Success.
- **HIXL_PARAM_INVALID**: Invalid parameter.
- Other values: Failure.

**Constraints**

- Use this API together with **HixlCSClientCreate**.
- It is recommended that you disconnect connections and unregister memory before calling **HixlCSClientDestroy**.
- This API cannot be called concurrently with other APIs.

## Usage Recommendations

1. Prepare the environment: Call `aclrtSetDevice(device_id)`.
2. Prepare Endpoint descriptions (`EndpointDesc`).
3. Server procedure:

   - Construct `HixlServerDesc` and `HixlServerConfig`. To specify a communication resource listening port or concurrently active device transfer channels, configure `comm_resource_config.listen_port` or `comm_resource_config.max_active_channels` in `HixlServerConfig.global_resource_config`.
   - Call `HixlCSServerCreate` to create a `HixlServerHandle`.
   - Allocate memory for remote access, prepare a `CommMem` descriptor (Host/Device memory), call `HixlCSServerRegMem`, and save the returned `MemHandle`.
   - Call `HixlCSServerListen` to start listening.
   - Wait for Clients to connect and initiate data transfers.
   - After transfers finish, call `HixlCSServerUnregMem` to unregister memory and `HixlCSServerDestroy` to destroy the service.

4. Client procedure:

   - Construct `HixlClientDesc` and `HixlClientConfig`. To specify QoS or concurrently active device transfer channels, configure `comm_resource_config.qos` or `comm_resource_config.max_active_channels` in `HixlClientConfig.global_resource_config`.
   - Call `HixlCSClientCreate` to create a `HixlClientHandle`.
   - Prepare local `CommMem` and register it with `HixlCSClientRegMem` (save the `MemHandle`).
   - Call `HixlCSClientConnect` to connect (blocking until completion or timeout). Ensure that the Server is listening.
   - Call `HixlCSClientGetRemoteMem` to obtain Server-registered memory and remote addresses for subsequent operations.
   - Construct a set of `HixlOneSideOpDesc` entries (local/remote addresses and lengths), then choose either:
     - Call `HixlCSClientBatchPutAsync` / `HixlCSClientBatchGetAsync` to submit asynchronous batch operations, then poll `HixlCSClientQueryCompleteStatus` until `HIXL_COMPLETE_STATUS_COMPLETED`;
     - Or call `HixlCSClientBatchPutSync` / `HixlCSClientBatchGetSync` to complete the batch synchronously (specify the batch timeout `timeout_ms`).
   - Read or verify memory as needed, then call `HixlCSClientUnregMem` to unregister local memory and `HixlCSClientDestroy` to clean up.

## Examples

The following simplified snippets show the key steps and order in the benchmark program, omitting detailed error handling and platform initialization:

```c
// --- Server (pseudocode) ---
HixlServerHandle server = NULL;
HixlServerDesc sdesc = {};
HixlServerConfig sconfig = {};
sconfig.global_resource_config = "{\"comm_resource_config.listen_port\":26666}";
sdesc.endpoint_list = &endpoint;
sdesc.server_ip = "0.0.0.0";
sdesc.server_port = 12345;
sdesc.endpoint_list_num = 1;
HixlCSServerCreate(&sdesc, &sconfig, &server);

// Allocate and initialize server memory (Host or Device).
CommMem server_mem = { .addr = server_buf, .size = size, .type = COMM_MEM_TYPE_DEVICE };
MemHandle server_mem_h = NULL;
HixlCSServerRegMem(server, "server_mem", &server_mem, &server_mem_h);

HixlCSServerListen(server, 1024);

// Wait for the client to initiate and complete transfers (TCP signaling can synchronize this).

// Unregister and destroy.
HixlCSServerUnregMem(server, server_mem_h);
HixlCSServerDestroy(server);

// --- Client (pseudocode) ---
HixlClientHandle client = NULL;
HixlClientDesc cdesc = {};
cdesc.local_endpoint = &local_ep;
cdesc.remote_endpoint = &remote_ep;
cdesc.server_ip = "server.ip";
cdesc.server_port = 12345;
HixlClientConfig cconfig = {};
cconfig.global_resource_config = "{\"comm_resource_config.qos\":7}";
HixlCSClientCreate(&cdesc, &cconfig, &client);

// Allocate and register local memory.
CommMem client_mem = { .addr = client_buf, .size = size, .type = COMM_MEM_TYPE_DEVICE };
MemHandle client_mem_h = NULL;
HixlCSClientRegMem(client, "client_mem", &client_mem, &client_mem_h);

// Establish a connection.
HixlCSClientConnect(client, 5000);

// Obtain remote memory addresses using mem_tag.
CommMem *remote_list = NULL; char **tags = NULL; uint32_t num = 0;
HixlCSClientGetRemoteMem(client, &remote_list, &tags, &num, 2000);

// Construct batch operation descriptors, for example, a loop with varying block size.
std::vector<HixlOneSideOpDesc> ops = ...; // Set local/remote addr and len.

// Method 1: Asynchronous submission and completion polling.
CompleteHandle ch;
HixlCSClientBatchPutAsync(client, ops.size(), ops.data(), &ch);
HixlCompleteStatus st;
do {
  HixlCSClientQueryCompleteStatus(client, ch, &st);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
} while (st == HIXL_COMPLETE_STATUS_WAITING);

// Method 2: Synchronous transfer (returns HIXL_SUCCESS if the batch completes within timeout).
// HixlCSClientBatchPutSync(client, ops.size(), ops.data(), 5000);

// Clean up.
HixlCSClientUnregMem(client, client_mem_h);
HixlCSClientDestroy(client);
```
