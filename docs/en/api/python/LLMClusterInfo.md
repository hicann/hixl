# LLMClusterInfo

<!-- md-trans-meta sourceCommit=6629c88eeb37cb5c42eb0fa4134dddd4d9208cd4 translatedAt=2026-10-21T07:15:41.595Z pushedAt=2026-10-27T02:46:32.331Z nl=2 -->

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
<!-- npu="910b" id4 -->
Note: For Atlas A2 products, only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported.
<!-- end id4 -->

## LLMClusterInfo Constructor

**Description**

Constructs **LLMClusterInfo**, a parameter type for **link\_clusters** and **unlink\_clusters**.

**Prototype**

```python
__init__()
```

**Parameters**

None

**Example**

```python
from llm_datadist import LLMClusterInfo
llm_cluster = LLMClusterInfo()
```

**Return Value**

Returns an **LLMClusterInfo** instance.

**Constraints**

None

## remote\_cluster\_id

**Description**

Sets the peer cluster ID.

**Prototype**

```python
remote_cluster_id(remote_cluster_id)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| remote_cluster_id | int | Peer cluster ID. |

**Example**

```python
from llm_datadist import LLMClusterInfo

llm_cluster = LLMClusterInfo()
llm_cluster.remote_cluster_id = 1
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## append\_local\_ip\_info

**Description**

Adds local cluster IP information.

**Prototype**

```python
append_local_ip_info(self, ip: Union[str, int], port: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| ip | Union[str, int] | Local cluster Host NIC IP address. |
| port | int | Local cluster Host NIC port. |

**Example**

```python
from llm_datadist import LLMClusterInfo

llm_cluster = LLMClusterInfo()
llm_cluster.append_local_ip_info("1.1.1.1", 10000)
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None

## append\_remote\_ip\_info

**Description**

Adds remote cluster IP information.

**Prototype**

```python
append_remote_ip_info(self, ip: Union[str, int], port: int)
```

**Parameters**

| Parameter | Data Type | Values |
| --- | --- | --- |
| ip | Union[str, int] | Host IP address. |
| port | int | Host port. |

**Example**

```python
from llm_datadist import LLMClusterInfo

llm_cluster = LLMClusterInfo()
llm_cluster.append_remote_ip_info("1.1.1.1", 10000)
```

**Return Value**

Returns nothing on success.

Invalid parameters may raise **TypeError** or **ValueError**.

**Constraints**

None
