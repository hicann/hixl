# ADXL APIs (To Be Deprecated)

<!-- md-trans-meta sourceCommit=1621eb44f186cc810cee6872bcbe4575ad4f0cfd translatedAt=2026-11-19T01:01:42.755Z pushedAt=2026-11-19T01:04:51.374Z nl=2 -->

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

<!-- npu="910b" id8 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id8 -->

## AdxlEngine Constructor

**Description**

Creates an **AdxlEngine** object.

**Prototype**

```cpp
AdxlEngine()
```

**Parameters**

None

**Return Value**

None

**Exception Handling**

None

**Constraints**

None

## \~AdxlEngine\(\)

**Description**

Destructor of an **AdxlEngine** object.

**Prototype**

```cpp
~AdxlEngine()
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

Initializes **AdxlEngine**. Call this API before calling other APIs.

**Prototype**

```cpp
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**Parameters**

|Parameter|Input/Output|Description|
|--|--|--|
|local_engine|Input|**AdxlEngine** identifier, which must be unique among all engines participating in connection establishment. The format is **host_ip:host_port** or **host_ip**. A loopback IP address is not recommended because it may conflict when multiple **AdxlEngine** instances interact. If **host_port** is set and **host_port>0**, the current **AdxlEngine** acts as a Server and listens on the configured port. If **host_port** is omitted or **host_port<=0**, it acts as a Client and does not listen.|
|options|Input|Initialization options. For details, see Table 1.|

**Table 1** options

|Parameter|Optional/Required|Description|
|--|--|--|
|OPTION_BUFFER_POOL|Optional|String value **"adxl.BufferPool"**. Configures the staging memory pool for transfers requiring staging buffers: when direct Host To Host transfers over HCCS are unsupported, when RDMA registration of Host memory is size-limited, or when staging improves performance for multiple small memory blocks (for example, 128K). The value format is **"$BUFFER_NUM:$BUFFER_SIZE"**. **The system default is "4:8" (in MB).** Set it to **"0:0"** to disable the staging memory pool. For concurrent transfers, increasing **$BUFFER_NUM** is recommended. Configure the same value wherever this option is used.|
|OPTION_RDMA_TRAFFIC_CLASS|Optional|String value **"adxl.RdmaTrafficClass"**. Configures the traffic class of the RDMA NIC. It serves the same purpose as **HCCL_RDMA_TC**. If both are set, this option takes precedence; otherwise, the configured value applies. The value range is [0,255], and the value must be a multiple of 4. The default is 132. For details, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/9.3.0-beta.2/docs/en/user_guide/hccl_env/README.md).|
|OPTION_RDMA_SERVICE_LEVEL|Optional|String value **"adxl.RdmaServiceLevel"**. Configures the service level of the RDMA NIC. It serves the same purpose as **HCCL_RDMA_SL**. If both are set, this option takes precedence; otherwise, the configured value applies. The value range is [0, 7], and the default is 4. For details, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/9.3.0-beta.2/docs/en/user_guide/hccl_env/README.md).|
|OPTION_LOCAL_COMM_RES|Optional|String value **"adxl.LocalCommRes"**. Configures local communication resources as a JSON string.|
|OPTION_AUTO_CONNECT|Optional|String value **"AutoConnect"**.<br>- **0**: Disables AutoConnect mode.<br>- **1**: Enables AutoConnect mode.<br><br>With this option enabled, transfers can start without explicitly establishing connections. Abnormal connections are automatically cleaned up after transfer errors or peer destruction. Peer destruction is detected through heartbeats, with a default interval of 10s.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

1. Use this API together with **Finalize**. After successful initialization, call **Finalize** before any exit to release resources. Otherwise, resources may be released in an unexpected order, causing errors.
2. Call **aclrtSetDevice** before initialization.

## Finalize

**Description**

Cleans up **AdxlEngine** resources.

**Prototype**

```cpp
void Finalize()
```

**Parameters**

None

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

**Return Value**

None

**Exception Handling**

None

**Constraints**

- Use this API together with **Initialize**.
- It is recommended that you disconnect all connections and deregister registered memory before calling **Finalize**.
- The Server must call this API after all Clients disconnect. If the Server exits earlier, errors occur during Client disconnection and data transfer.
- If a Client reads or writes remote Server addresses, the Server must wait until the Client finishes these operations before calling this API. Otherwise, the operations fail.
- This API cannot be called concurrently with other APIs.

## RegisterMem

**Description**

Registers memory addresses for use as local and remote addresses in **TransferSync** calls. The addresses specified in **TransferSync** may be subsets of the registered regions. Local memory addresses must be registered with the current **AdxlEngine**, and remote memory addresses must be registered with the remote **AdxlEngine**.

**Prototype**

```cpp
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**Parameters**

|Parameter|Input/Output|Description|
|--|--|--|
|mem|Input|Description of the memory to register. Type: **MemDesc**.|
|type|Input|Type of the memory to register. Type: **MemType**.|
|mem_handle|Output|Memory handle returned after successful registration, used for memory deregistration. Type: **MemHandle**.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- Other values: Failure.

**Exception Handling**

None

**Constraints**

- Register all local memory before calling **Connect** to establish a connection with the peer.
- It is recommended that you register no more than 4K memory regions per instance. Excessive registrations may cause device OOM and increase connection establishment time, potentially causing connection timeouts. Control the number and size of registered regions based on your application.
- Up to 50GB of Device memory and 20GB of Host memory can be registered. Registering more memory consumes more OS memory.
- Allocate Host memory to register using **aclrtMallocHost**. Memory addresses allocated by this API are automatically aligned.
- Device memory to be registered must be allocated using **aclrtMalloc**. For HCCS transfers, set the allocation policy to **ACL\_MEM\_MALLOC\_HUGE\_ONLY**.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## DeregisterMem

**Description**

Deregisters memory.

**Prototype**

```cpp
Status DeregisterMem(MemHandle mem_handle)
```

**Parameters**

|Parameter|Input/Output|Description|
|--|--|--|
|mem_handle|Input|Memory handle returned by **RegisterMem** during memory registration.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

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

Establishes a connection with a remote **AdxlEngine**.

**Prototype**

```cpp
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

|Parameter|Input/Output|Description|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote **AdxlEngine**. The **AdxlEngine** corresponding to **remote_engine** must be the same Server.|
|timeout_in_millis|Input|Connection establishment timeout, in ms. Default: 1000.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

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
- A maximum of 512 communications can be created. Excessive connections may cause memory OOM and degrade KV Cache transfer performance.
- A timeout of at least 200ms is recommended. If TLS is enabled, a timeout of at least 2000ms is recommended. Use the following command to query the TLS status:

    hccn\_tool \[-i %d\] -tls -g \[host\]

- Register all local and remote memory before calling this API. Memory registered after connection establishment does not support remote access.
- In containers, map **/etc/hccn.conf** into the container or ensure that **hccn\_tool** exists under the default path **/usr/local/Ascend/driver/tools**. If neither condition is met, add the directory containing **hccn\_tool** to **PATH**. The following example uses **hccn\_tool\_install\_path** for that directory.

    ```sh
    export PATH=$PATH:${hccn_tool_install_path}
    ```

<!-- npu="A3,910b" id9 -->
- For Device RoCE, use consistent Device RoCE address configurations throughout the communication cluster. Mixing IPv6-only nodes with IPv4/IPv6 dual-stack nodes is unsupported. This constraint applies to the following products:
  <!-- npu="910b" id10 -->
  - Atlas A2 products
  <!-- end id10 -->
  <!-- npu="A3" id11 -->
  - Atlas A3 products
  <!-- end id11 -->
<!-- end id9 -->

- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## Disconnect

**Description**

Disconnects from a remote **AdxlEngine**.

**Prototype**

```cpp
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote **AdxlEngine**.|
|timeout_in_millis|Input|Disconnection timeout, in ms. Default: 1000.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- Other values: Failure.

**Constraints**

- Call **Initialize** to complete initialization before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## TransferSync

**Description**

Transfers memory with a remote **AdxlEngine**.

**Prototype**

```cpp
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote **AdxlEngine**.|
|operation|Input|Reads remote memory into local memory or writes local memory to remote memory.|
|op_descs|Input|Local and remote addresses for batch operations.|
|timeout_in_millis|Input|Transfer timeout, in ms. Default: 1000.|

**Example**

Visit [Gitee](https://gitee.com/ascend/samples/tags), download the sample package matching your version based on the tag name, and obtain the example from **cplusplus/level1\_single\_api/12\_adxl**.

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter.
- **NOT\_CONNECTED**: No connection to the peer.
- **TIMEOUT**: Transfer timed out.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- The staging memory pool is enabled by default. If **op\_descs** contains data smaller than 256K, staging transfers are used by default to improve performance. Otherwise, staging or direct transfer is selected based on whether unregistered memory is present.
- With the staging memory pool enabled, a staging transfer is used if either local or remote memory in **op\_descs** is unregistered. Unregistered memory is treated as Host memory. Ensure that its address is valid.
- In staging transfer mode, all **op\_descs** must use the same transfer type. For example, all operations in **op\_descs** write from local Host memory to remote Host memory.

## TransferAsync

**Description**

Performs batch asynchronous memory transfers with a remote **AdxlEngine**.

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
| remote_engine | Input | Unique identifier of the remote **AdxlEngine**. |
| operation | Input | Reads remote memory into local memory or writes local memory to remote memory. |
| op_descs | Input | Local and remote addresses for batch operations. |
| optional_args | Input | Optional parameter (reserved). |
| req | Output | Request handle used to query transfer request status. |

**Example**

```cpp
  //Initialize the client and server engines and establish a connection.
  client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
```

**Return Value**

- **SUCCESS**: Success.
- **PARAM\_INVALID**: Invalid parameter (**local_addr** or **remote_addr** in **op_descs** is **null**).
- **NOT\_CONNECTED**: No connection to the peer.
- **RESOURCE_EXHAUSTED**: Resources exhausted.
- Other values: Failure.

**Constraints**

- The following constraints apply before calling this API:
  - Call **Connect** to establish a connection with the peer first.
  <!-- npu="A3,910b" id4 -->
  - Alternatively, enable the connection pool during HIXL initialization by configuring **OPTION_GLOBAL_RESOURCE_CONFIG** in **options**. This constraint applies to the following products:
    <!-- npu="910b" id5 -->
    - Atlas A2 products
    <!-- end id5 -->
    <!-- npu="A3" id6 -->
    - Atlas A3 products
    <!-- end id6 -->
  <!-- end id4 -->
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- Currently, asynchronous transfers support only direct transfers. Staging transfers are unsupported. Direct transfer is the default.
  <!-- npu="A3" id7 -->
- In Fabric Mem transfer mode, all op_descs must have the same transfer type. The system determines the transfer direction from the memory type of the first op_desc. This constraint applies to the following products:
  - Atlas A3 products
  <!-- end id7 -->

## GetTransferStatus

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
  Status query_status = GetTransferStatus(req, status);
  //Check the transfer status to determine whether the transfer is complete.
  ...
```

**Return Value**

- **SUCCESS**: Success.
- **FAILED**: Invalid parameter (for example, **req** is **null**) or transfer failure.
- **NOT\_CONNECTED**: No connection to the peer.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- After calling **TransferAsync**, use this API to query the request status. If the status is **COMPLETED** or **FAILED**, related resources are released and the request cannot be queried again.
- For asynchronous transfers, determine timeouts in your application. If a task times out, it is recommended that you call **Disconnect** to destroy the connection and clean up related resources.
- After an asynchronous transfer fails, both the queried status and the return status of this API are **FAILED**.

## SendNotify

**Description**

Sends a Notify message to a remote engine.

**Prototype**

```cpp
Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis = 1000)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|remote_engine|Input|Unique identifier of the remote **AdxlEngine**.|
|notify|Input|Notify content to send. Both **name** and **notify_msg** have a maximum length of 1024 bytes, measured in storage bytes by **AscendString.GetLength()**. The character count of a multibyte encoding is not this length.|
|timeout_in_millis|Input|Send timeout, in ms. Default: 1000.|

**Example**

None

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.
- Each connection supports up to 4096 Notify messages. Ensure that the remote **AdxlEngine** promptly calls **GetNotifies** to consume them, preventing send failures caused by reaching the limit.

## GetNotifies

**Description**

Obtains Notify messages received by all Servers in the current **AdxlEngine** and clears the received messages.

**Prototype**

```cpp
Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|notifies|Output|Vector for storing Notify messages.|

**Example**

None

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

- Call **Connect** to establish a connection with the peer before calling this API.
- Run this API in the same thread as **Initialize**. To call it in another thread, call **aclrtGetCurrentContext** in the thread running **Initialize** to obtain the context, then call **aclrtSetCurrentContext** in the new thread to set the context.

## MallocMem

**Description**

Allocates memory.

**Prototype**

```cpp
static Status MallocMem(MemType type, size_t size, void **ptr);
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|type|Input|Memory type.|
|size|Input|Memory size.|
|ptr|Output|Pointer to the allocated memory.|

**Example**

None

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

Only Host memory allocation is supported.

## ExportToShareableHandle

**Description**

Exports memory allocated by **MallocMem** as a Fabric shareable handle. The caller can pass it to other processes to import and share the same physical memory. The first call for a region exports and caches the handle. Subsequent calls return the cached handle.

**Prototype**

```cpp
static Status ExportToShareableHandle(void *addr, ShareableHandle &handle);
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|addr|Input|Memory address returned by **MallocMem**.|
|handle|Output|Exported Fabric shareable handle. Type: **ShareableHandle**.|

**Example**

None

**Return Value**

- **SUCCESS**: Success.
- **PARAM_INVALID**: **addr** is null, is not an address returned by **MallocMem**, or refers to memory that has been freed.
- Other values: Failure.

**Constraints**

- **addr** must be the starting address returned by **MallocMem**. An address within the region is unsupported.
- Call this API before calling **FreeMem** to free the corresponding memory.
- The region is exported only on the first call. Repeated calls return the same shareable handle.

## FreeMem

**Description**

Frees memory.

**Prototype**

```cpp
static Status FreeMem(void *ptr)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|ptr|Input|Pointer to the memory to free.|

**Example**

None

**Return Value**

- **SUCCESS**: Success.
- Other values: Failure.

**Constraints**

None

## GetCapability

**Description**

Queries library capabilities. The upper layer can call this API before **Initialize** to detect whether the current **AdxlEngine** library supports capabilities such as Auto Connect and Client/Server communication, avoiding hard-coded defaults or incompatibility with older **.so** files.

**Prototype**

```cpp
static Status GetCapability(FeatureType feature_type, int32_t &value)
```

**Parameters**

|**Parameter**|Input/Output|**Values**|
|--|--|--|
|feature_type|Input|Capability type. For values, see **FeatureType**.|
|value|Output|Capability support status. **1** means supported (**FEATURE_SUPPORTED**); **0** means unsupported (**FEATURE_NOT_SUPPORTED**).|

**Example**

None

**Return Value**

- **SUCCESS**: Success. For unknown or unsupported capability types, `value` is `FEATURE_NOT_SUPPORTED`.
- **PARAM_INVALID**: Invalid parameter (**feature_type** is negative).

**Constraints**

- This API can be called without calling **Initialize**.
- Static method independent of an **AdxlEngine** instance.
