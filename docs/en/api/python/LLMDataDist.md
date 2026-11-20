# LLMDataDist

<!-- md-trans-meta sourceCommit=d14128d33cad9351db492e9412c3f829e884d483 translatedAt=2026-10-28T08:54:27.650Z pushedAt=2026-10-28T08:59:28.969Z nl=2 -->

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

Note:
<!-- npu="910b" id4 -->
For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id4 -->
<!-- npu="950" id5 -->
Ascend 950PR&950DT products do not support **link**, **unlink**, or **query_register_mem_status**.
<!-- end id5 -->

## LLMDataDist Constructor

**Description**

Constructs **LLMDataDist**.

**Prototype**

```python
__init__(role: LLMRole, cluster_id: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| role | LLMRole | Cluster role. This parameter only identifies the current role and does not affect the transfer process.<br><br>  - **LLMRole.DECODER**: Decode cluster.<br>  - **LLMRole.PROMPT**: Prompt cluster. |
| cluster_id | int | Cluster ID identifying **LLMDataDist**. Must be unique among all instances participating in connection establishment. |

**Example**

```python
from llm_datadist import LLMDataDist, LLMRole
llm_datadist = LLMDataDist(LLMRole.DECODER, 0)
```

**Return Value**

Returns an **LLMDataDist** instance on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## init

**Description**

Initializes **LLMDataDist**. Configure CacheManager mode in the **options** parameter of **init**, setting **enable\_cache\_manager** to **True** or specifying **local\_comm\_res**.

In this mode, Decode and Prompt can pull Caches from each other.

**Prototype**

```python
init(options: Dict[str, str])
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| options | Dict[str, str] | Settings.<br>Generate **options** through **LLMConfig**.<br><br>  - **device_id** is required.<br>  - Set **enable_cache_manager** to **True** or configure **local_comm_res**. |

To enable control plane TLS, configure `tls_config.*` through [LLMConfig.global_resource_config](LLMConfig.md#global_resource_config).

**Example**

```python
from llm_datadist import LLMDataDist, LLMRole, LLMConfig
llm_datadist = LLMDataDist(LLMRole.PROMPT, 0)
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
llm_config.device_id = 0
engine_options = llm_config.generate_options()
llm_datadist.init(engine_options)
```

**Return Value**

Returns nothing on success.

Raises **LLMException** on error.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

After successful initialization, call **finalize** before system exit.

## finalize

**Description**

Releases **LLMDataDist**.

**Prototype**

```python
finalize()
```

**Parameters**

None

**Example**

```python
from llm_datadist import LLMDataDist, LLMRole, LLMConfig
llm_datadist = LLMDataDist(LLMRole.DECODER, 0)
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
llm_config.device_id = 0
engine_options = llm_config.generate_options()
llm_datadist.init(engine_options)
llm_datadist.finalize()
```

**Return Value**

None

**Constraints**

- After successful initialization, call **finalize** before system exit.

- **finalize** cannot be called concurrently with other APIs.

## link\_clusters

**Description**

Establishes a one-sided connection initiated by the Client. Client/Server identity is independent of the prompt/decoder role. Setting **listen\_ip\_info** enables port listening and identifies a Server.

**Prototype**

```python
link_clusters(clusters: Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]], timeout=3000)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| clusters | Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]] | Cluster list. |
| timeout | int | Timeout, in ms. Default: 3000ms. |

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

Returns a tuple of two values: the API return status (**LLMStatusCode**) and a list of per-cluster connection results (**LLMStatusCode**).

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

- Connection requirements are as follows. Excessive connections may cause memory OOM and degrade KV Cache transfer performance.
  - When **local_comm_res** is empty or **version** is **"1.0"** or **"1.2"**, connections use collective communication communicators, with a maximum of 512 communications.
  - With **local_comm_res** version **"1.3"** (recommended; requires HDK 25.5.0 or later and toolkit 9.1.0 or later), connections use HixlCS without a connection limit.

- We recommend setting the timeout to at least 200ms, or at least 2000ms if Device TLS or control plane TLS is enabled. Use the following command to query Device TLS status. Control plane TLS status depends on whether `tls_config.*` is configured:

    ```sh
    hccn_tool [-i %d] -tls -g [host]
    ```

- Register all memory before this API. Memory registered after connection establishment does not support remote access.
- In containers, if **local\_comm\_res** is omitted or empty, map **/etc/hccn.conf** into the container or ensure that **hccn_tool** exists under **/usr/local/Ascend/driver/tools**. Otherwise, add its directory to **PATH**. In the example below, **hccn_tool_install_path** is that directory.

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

## unlink\_clusters

**Description**

Performs one-sided disconnection. The Client can notify the Server to disconnect, or both Client and Server can force disconnection to clean up only their local connections.

**Prototype**

```python
unlink_clusters(clusters: Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]], timeout=3000, force=False)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| clusters | Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]] | Cluster list. |
| timeout | int | Timeout, in ms. Default: 3000ms. |
| force | bool | Whether to force disconnection. Default: **False**; **True** forces disconnection.<br><br>  - Forced disconnection removes only local connections; both ends must call it.<br>  - Normal disconnection starts on the Client.<br>  - Without faults, both ends are disconnected.<br>  - If a fault causes failure, also initiate disconnection on the Server. |

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

Returns a tuple of two values: the API return status (**LLMStatusCode**) and a list of per-cluster disconnection results (**LLMStatusCode**).

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## switch\_role

**Description**

Switches the current **LLMDataDist** role. **switch\_options** can also switch Client/Server mode.

**Prototype**

```python
switch_role(self, role: LLMRole, switch_options: Optional[Dict[str, str]] = None)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| role | LLMRole | Target role. |
| switch_options | options: Dict[str, str] | Optional role switching settings, default **None**.<br>For a Server, configure the listening Host IP and port through **listen_ip_info**, for example, **"192.168.1.1:26000"**. Otherwise, the instance acts as a Client.<br>Switching from Server to Client closes the old listening port. To change only the Server port, specify another port in **options**; the old port closes.<br>With the hixl backend, **switch\_role** cannot change the listening port. |

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

- Returns nothing on success.
- Raises **TypeError** or **ValueError** for an incorrect input data type.
- If connection resources remain, **switch\_role** raises **LLMException** with **status\_code LLM\_EXIST\_LINK**.

**Constraints**

None

## link

**Description**

Establishes a two-sided connection through a communicator. The easier one-sided **link\_clusters** API is recommended and requires no ranktable.

For RDMA NIC communication within a single server, set **HCCL\_INTRA\_ROCE\_ENABLE=1**.

**Prototype**

```python
link(comm_name: str, cluster_rank_info: Dict[int, int], rank_table: str) -> int
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| comm_name | str | Communicator name.<br>Must be shorter than 128 characters. |
| cluster_rank_info | Dict[int, int] | Cluster ID to rank ID mapping.<br>Example: **{1: 0, 2: 1}**. |
| rank_table | str | Developers can use this parameter to configure NPU resource information for collective communication.

For details about ranktable in the preceding table, see [HCCL Collective Communication Library](https://gitcode.com/cann/hccl/blob/master/docs/en/user_guide/README.md).<br>Example **rank_table** configuration:

```sh
{
    "server_count": "2",
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x",
                    "rank_id": "0"
                },
            ],
            "server_id": "xxxx"
        },{
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x",
                    "rank_id": "1"
                },
            ],
            "server_id": "xxxx"
        }
    ],
    "status": "completed",
    "version": "1.0"
}
```

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

Returns the communicator ID on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

- All nodes in the communicator must initiate this operation simultaneously.
- A communicator supports up to 4 nodes.
- Using no more than 16 communicators is recommended; the maximum is 512. Excessive connections may cause memory OOM and degrade cache transfer performance.
- **link** exchanges memory descriptors within the communicator. For caches used in transfers, configure a memory pool or register memory before **link**.
- Calls from forked child processes are unsupported.
- Up to 16 connections can be established concurrently. Additional requests are queued internally.
- Avoid cyclic dependencies when establishing multiple communicators.
<!-- npu="950" id6 -->
- Ascend 950PR&950DT products do not support this API.
<!-- end id6 -->

## unlink

**Description**

Disconnection API corresponding to a two-sided connection. Call this API to disconnect.

**Prototype**

```python
unlink(comm_id: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| comm_id | int | Return value of **link**. |

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

Returns nothing on success.

Raises **LLMException** on error.

Invalid parameters may raise **TypeError** or **ValueError**.

<!-- npu="950" id7 -->
Ascend 950PR&950DT products do not support this API.
<!-- end id7 -->

**Constraints**

None

## query\_register\_mem\_status

**Description**

Queries registered memory status.

**Prototype**

```python
query_register_mem_status(comm_id: int) -> RegisterMemStatus
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| comm_id | int | Return value of **link**. |

**Example**

See [Running the Examples](../../../../examples/python/README.md).

**Return Value**

Normally returns a **RegisterMemStatus** value.

Raises **LLMException** on error.

Invalid parameters may raise **TypeError** or **ValueError**.

<!-- npu="950" id8 -->
Ascend 950PR&950DT products do not support this API.
<!-- end id8 -->

**Constraints**

Use with **link**.

## cache\_manager

**Description**

Obtains a **CacheManager** instance.

**Prototype**

```python
cache_manager()
```

**Parameters**

None

**Example**

```python
from llm_datadist import LLMDataDist, LLMRole
llm_datadist = LLMDataDist(LLMRole.DECODER, 0)
...
llm_datadist.init(engine_options)
cache_manager = llm_datadist.cache_manager
```

**Return Value**

Returns a **CacheManager** instance.

**Constraints**

None
