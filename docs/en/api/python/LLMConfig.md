# LLMConfig

<!-- md-trans-meta sourceCommit=d14128d33cad9351db492e9412c3f829e884d483 translatedAt=2026-10-28T08:54:27.647Z pushedAt=2026-10-28T08:59:28.930Z nl=2 -->

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

<!-- npu="910b" id14 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id14 -->

## LLMConfig Constructor

**Description**

Constructs **LLMConfig**. **init** requires a configuration dictionary; use this class to build it more easily.

Decode and Prompt can pull Caches from each other. Related APIs include **enable\_cache\_manager**, **mem\_pool\_cfg**, and **listen\_ip\_info**.

**Prototype**

```python
__init__()
```

**Parameters**

None

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
```

**Return Value**

Returns an **LLMConfig** instance.

**Constraints**

None

## generate\_options

**Description**

Generates a configuration dictionary.

**Prototype**

```python
generate_options()
```

**Parameters**

None

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
...
engine_options = llm_config.generate_options()
```

**Return Value**

Returns the configuration dictionary.

**Constraints**

None

## device\_id

**Description**

Sets the current process Device ID, corresponding to **ge.exec.deviceId**.

**Prototype**

```python
device_id(device_id)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| device_id | Union[int, List[int], Tuple[int]] | Sets the Device ID of the current process. Currently, only one Device ID can be configured. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.device_id = 0
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## sync\_kv\_timeout

**Description**

Configures timeouts for **pull\_cache**, **pull\_blocks**, **push\_blocks**, and **push\_cache**, corresponding to **llm.SyncKvCacheWaitTime**.

**Prototype**

```python
sync_kv_timeout(sync_kv_timeout)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| sync_kv_timeout | int or str | Synchronous KV timeout, in ms. Default: 1000ms. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.sync_kv_timeout = 1000
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## ge\_options

**Description**

Configures additional GE settings.

**Prototype**

```python
ge_options(ge_options)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| ge_options | dict[str, str] | GE settings.<br>**ge.flowGraphMemMaxSize** is particularly relevant: it specifies the maximum total memory used by all KV caches. An excessive value reduces memory available to the model. Set it based on your needs. |

**Example**

```python
from llm_datadist import LLMConfig
ge_options = {
    "ge.flowGraphMemMaxSize": "4106127360"
}
llm_config = LLMConfig()
llm_config.ge_options = ge_options
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## listen\_ip\_info

**Description**

Sets cluster listening information, corresponding to **llm.listenIpInfo**.

**Prototype**

```python
listen_ip_info(listen_ip_info)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| listen_ip_info | str | Supports configuring one Host IP address and port, such as "192.168.1.1:26000". |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.listen_ip_info = "192.168.1.1:26000"
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## enable\_cache\_manager

**Description**

Configures whether CacheManager mode is enabled, corresponding to **llm.EnableCacheManager**.

**Prototype**

```python
enable_cache_manager(self, enable_cache_manager: bool)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| enable_cache_manager | bool | Whether to enable CacheManager mode. Must be enabled.<br><br>  - **True**: Enabled.<br>  - **False**: Disabled (default if omitted). |

<!-- npu="950" id4 -->
**False** is unsupported on Ascend 950PR&950DT products.
<!-- end id4 -->

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## enable\_remote\_cache\_accessible

**Description**

Configures whether to enable direct access to remote Caches.

Enabling this option caches remote Cache metadata (indices, memory addresses, and so on) locally to accelerate Pull. It also introduces constraints; see Constraints.

**Prototype**

```python
enable_remote_cache_accessible(self, enable_remote_cache_accessible: bool)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| enable_remote_cache_accessible | bool | Whether to enable direct remote Cache access.<br><br>  - **True**: Enabled.<br>  - **False**: Disabled.<br><br>Default: Disabled.|

<!-- npu="A3" id5 -->
Without this option, Atlas A3 products support only RDMA. Enabling it is recommended to support more transport protocols.
<!-- end id5 -->
<!-- npu="950" id6 -->
<br>**False** is unsupported on Ascend 950PR&950DT products.
<!-- end id6 -->

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_remote_cache_accessible = True
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

Indices update only on the first Pull. Subsequent peer Cache operations are invisible locally; ensure Cache validity. This option is best suited to PA, where Caches are allocated/registered during script initialization and rarely change.

## rdma\_traffic\_class

**Description**

Configures the RDMA NIC traffic class.

**Prototype**

```python
rdma_traffic_class(self, rdma_traffic_class: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| rdma_traffic_class | int | RDMA NIC traffic class, equivalent to **HCCL_RDMA_TC**. If both are set, this option takes precedence; otherwise, the configured value applies.<br>Range: [0,255], a multiple of 4. Default: 132. |

For the environment variables above, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/hccl_env/README.md).

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_traffic_class = 100
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## rdma\_service\_level

**Description**

Configures the RDMA NIC service level.

**Prototype**

```python
rdma_service_level(self, rdma_service_level: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| rdma_service_level | int | RDMA NIC service level, equivalent to **HCCL_RDMA_SL**. If both are set, this option takes precedence; otherwise, the configured value applies.<br>Range: [0, 7]. Default: 4. |

For the environment variables above, see [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/hccl_env/README.md).

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_service_level = 2
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## local\_comm\_res

**Description**

Configures local communication resources.

**Prototype**

```python
local_comm_res(local_comm_res)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| local_comm_res | str | Local communication resources as a JSON string.<br>- Omitted or empty: Automatically generates resources and uses collective communication communicators, limited to 512 connections per card. Note: With **buffer_pool** (or **adxl.BufferPool**) set to **"0:0"** (staging pool disabled) and hcomm/toolkit 9.1.0 or later, HixlCS is used without a connection limit.<br>- Ranktable with **version "1.0"** or **"1.2"**: Uses collective communication communicators, limited to 512 connections per card. Configure only Device information used by the current llm datadist; the **server_count** and **rank_id** fields need not be configured.<br>- **version "1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later): Uses HixlCS without a connection limit. For communication resource fields, see [HIXL API Reference](../cpp/HIXL-interface.md#communication-resource-configuration-fields).<br><br>If **enable_cache_manager** and **enable_remote_cache_accessible** are omitted, configuring this option defaults both to **True**.

<!-- npu="A3,910b" id11 -->
For Device RoCE, use consistent Device RoCE address configurations throughout the communication cluster. Mixing IPv6-only nodes with IPv4/IPv6 dual-stack nodes is unsupported. This constraint applies to the following products:

<!-- npu="910b" id12 -->
- Atlas A2 products
<!-- end id12 -->
<!-- npu="A3" id13 -->
- Atlas A3 products
<!-- end id13 -->
<!-- end id11 -->

For **version "1.3"**, see [GitCode](https://gitcode.com/cann/hixl/issues/38) for the format. When version is configured as "1.3", other fields are generated automatically. Example:

```sh
local_comm_res = '''{
    "version": "1.3"
}'''
```

<!-- npu="910b" id8 -->
On Atlas A2 products with **version "1.0"**, configure only the Device information used by the current llm datadist in the ranktable; the **server_count** and **rank_id** fields need not be configured. For ranktable details, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/README.md). Example:

```sh
{
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                }
            ],
            "server_id": "xxxx"
        }
    ],
    "status": "completed",
    "version": "1.0"
}
```

<!-- end id8 -->
This field may be empty. Example:

```sh
local_comm_res = ""
```

<!-- npu="A3" id9 -->
On Atlas A3 products with **version "1.2"**, configure only the Device information used by the current llm datadist in the ranktable; the **server_count** and **rank_id** fields need not be configured. For ranktable details, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/README.md). Example:

```sh
{
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                },
            ],
            "server_id": "xxxx"
        }
    ],
     "super_pod_list": [
        {
            "super_pod_id": "0",          // Unique supernode identifier.
            "server_list": [              // AI Server list within the supernode.
                {"server_id": "xxxx"}  // server_id identifies the Server and corresponds to server_id in "server_list".
            ]
        },
    ]
    "status": "completed",
    "version": "1.2"
}
```
<!-- end id9 -->

This field may be empty. Example:

```python
local_comm_res = ""
```

<!-- npu="950" id10 -->
For Ascend 950PR&950DT configuration, see [GitCode](https://gitcode.com/cann/hixl/issues/38). Also set **transfer_backend** to **hixl**. An empty configuration is unsupported.
<!-- end id10 -->

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.local_comm_res = '''{
    "version": "1.3"
}'''
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

The following constraints apply when this option is configured.

- When calling **link\_clusters** or **unlink\_clusters**, **append\_local\_ip\_info** in **clusters** is unnecessary if **local\_comm\_res** is nonempty, and required if it is empty.
- Setting **enable\_cache\_manager** or **enable\_remote\_cache\_accessible** to **False** is currently unsupported.

## link\_total\_time

**Description**

Configures the total timeout for failed HCCL connection establishment.

**Prototype**

```python
link_total_time(self, link_total_time: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| link_total_time | int | HCCL connection establishment timeout, in seconds. Retries follow the retry count within this total timeout.<br>Range: [0, 2^32-1], default 0. Optional; omission passes 0 automatically. With 0, **HCCL_CONNECT_TIMEOUT** is read, defaulting to 120s. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_total_time = 20
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## link\_retry\_count

**Description**

Configures the retry count for failed HCCL connection establishment.

**Prototype**

```python
link_retry_count(link_retry_count)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| link_retry_count | int | HCCL connection establishment retry count. Retries follow this count within the total timeout.<br>Range: [1, 100], default 1. Optional; omission passes 1 automatically. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_retry_count = 2
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## transfer\_backend

**Description**

Configures the transfer backend used by LLM-DataDist.

**Prototype**

```python
transfer_backend(transfer_backend)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| transfer_backend | str | Set to **hixl** to use hixl as the transfer backend engine. |

**Example**

```python
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.transfer_backend = "hixl"
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

- Specify **listen_ip_info** during initialization. With the hixl backend, each endpoint can act as both Client and Server.
- Call **link_clusters** to establish a connection before transferring with the peer.

## global\_resource\_config

**Description**

Configures global LLM-DataDist resources. `tls_config.*` configures control plane TLS independently of **transfer_backend**. Other fields are passed through to the HIXL engine for parsing and validation only when **transfer_backend** is **"hixl"**.

**Prototype**

```python
global_resource_config(global_resource_config)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| global_resource_config | str | JSON string. For `tls_config.*` fields, see the control plane TLS configuration below. For other fields and constraints, see [Initialize in the HIXL API Reference](../cpp/HIXL-interface.md#initialize). |

**Control Plane TLS Configuration**

| Field | Data Type | Required/Optional | Description |
| --- | --- | --- | --- |
| tls_config.cert_file | String | Required when TLS is enabled | Path to the control plane TLS certificate file. It must be configured together with `tls_config.key_file`. |
| tls_config.key_file | String | Required when TLS is enabled | Path to the control plane TLS private key file. It must be configured together with `tls_config.cert_file`. |
| tls_config.ca_file | String | Optional | Path to the CA file used to verify the peer certificate. Required when `tls_config.verify_peer` is true. |
| tls_config.verify_peer | Boolean | Optional | Whether to verify the peer certificate. Defaults to false. When false, an encrypted connection is established without verifying the peer identity. In production environments, we recommend setting it to true, in which case the peer must provide a valid certificate. The control plane establishes connections by IP address and does not perform hostname or SNI verification. |
| tls_config.ciphers | String | Optional | OpenSSL cipher suites; applies only to TLS 1.2. For example, `ECDHE-RSA-AES256-GCM-SHA384`. |
| tls_config.enable_cert_refresh | Boolean | Optional | Whether to automatically load changed certificate files. Defaults to false. When enabled, a new certificate applies only to connections established afterward; existing connections are unaffected. If certificate loading fails, the original certificate remains in use. |

**Example**

```python
import json

from llm_datadist import LLMConfig

llm_config = LLMConfig()
llm_config.global_resource_config = json.dumps({
    "tls_config.cert_file": "/path/to/cert.pem",
    "tls_config.key_file": "/path/to/key.pem",
    "tls_config.ca_file": "/path/to/ca.pem",
    "tls_config.verify_peer": True,
    "tls_config.enable_cert_refresh": True
})
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

- `tls_config.*` enables TCP TLS on the LLM-DataDist control plane. Control plane TLS protects only the control plane connections that LLM-DataDist establishes through HIXL, HIXL CS, or ADXL. It does not change how the KV Cache data plane transfers or encrypts data.
- Control plane TLS is disabled if no `tls_config.*` fields are configured.
- All LLM-DataDist, HIXL, HIXL CS, and ADXL instances in the same process must use the same control plane TLS configuration, including whether TLS is enabled. Inconsistent configurations cause initialization or object creation to fail.
- To change the control plane TLS configuration, first destroy all LLM-DataDist, HIXL, HIXL CS, and ADXL objects in the process that use this configuration, and then reinitialize them.
- `tls_config.cert_file` and `tls_config.key_file` must be configured together. When `tls_config.verify_peer` is true, `tls_config.ca_file` is required. An incorrect field type, a file loading failure, or a certificate/private key mismatch causes initialization to fail; it does not fall back to a plaintext connection.
- `tls_config.verify_peer` defaults to false. When it is true, the peer must provide a valid certificate. The control plane establishes connections by IP address and does not perform hostname or SNI verification.
- `tls_config.ciphers` applies only to TLS 1.2. When `tls_config.enable_cert_refresh` is enabled, a new certificate applies only to connections established afterward; existing connections are unaffected. If certificate loading fails, the original certificate remains in use.
- Other global resource fields apply only when **transfer_backend** is **"hixl"**.
- Omitting it retains default behavior and does not affect existing initialization.
- Invalid JSON, incorrect field types, or invalid values cause initialization to fail.
