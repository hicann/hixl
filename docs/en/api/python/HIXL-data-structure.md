# HIXL Data Structures

<!-- md-trans-meta sourceCommit=6e9de18392581c2cf328c136fea56595d8964202 translatedAt=2026-10-22T03:41:55.234Z pushedAt=2026-10-27T03:00:23.807Z nl=2 -->

## MemDesc

Memory description.

```python
class MemDesc:
    def __init__(self, addr: int, len: int)

    addr: int   # Memory address.
    len: int    # Memory length in bytes.
    local_only: bool  # Whether the memory is for local use only and is not exposed to remote peers. Default: False.
```

**Example**

```python
mem_desc = hixl.MemDesc(addr=0x7f0000000000, len=1024*1024)
```

## MemHandle

Memory handle.

In Python, it is of type int, returned by register\_mem.

## MemType

Memory type.

```python
class MemType:
    MEM_DEVICE = 0  # Device memory.
    MEM_HOST = 1    # Host memory.
```

## AsyncConnectStatus

Asynchronous connection/disconnection status.

```python
class AsyncConnectStatus:
    NOT_CONNECT = 0         # Not connected
    CONNECT_PENDING = 1     # Connection establishment pending
    CONNECTING = 2          # Connection establishment in progress
    CONNECTED = 3           # Connection established
    CONNECT_FAILED = 4      # Connection establishment failed
    DISCONNECT_PENDING = 5  # Disconnection pending
    DISCONNECTING = 6       # Disconnection in progress
```

## TransferOp

Transfer operation type.

```python
class TransferOp:
    READ = 0   # Read remote data into local memory.
    WRITE = 1  # Write local data to remote memory.
```

## TransferOpDesc

Transfer operation description.

```python
class TransferOpDesc:
    def __init__(self, local_addr: int, remote_addr: int, len: int)

    local_addr: int   # Local memory address.
    remote_addr: int  # Remote memory address.
    len: int          # Transfer length in bytes.
```

**Example**

```python
op_desc = hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=4096)
```

## TransferArgs

Optional transfer parameters.

```python
class TransferArgs:
    def __init__(self)

    user_data: int  # User-defined information (int), used with the API for querying all asynchronous transfer request statuses.
```

**Example**

```python
args = hixl.TransferArgs()
args.user_data = 12345
```

## TransferReq

Transfer request handle.

Python int returned by transfer\_async.

## TransferStatus

Asynchronous transfer status.

```python
class TransferStatus:
    WAITING = 0    # Waiting
    COMPLETED = 1  # Completed
    TIMEOUT = 2    # Timeout (currently unsupported)
    FAILED = 3     # Failed
```

## GetTransferStatusArgs

Parameters for querying all asynchronous transfer request statuses.

```python
class GetTransferStatusArgs:
    def __init__(self, max_query_count: int = 0xFFFFFFFF, skip_waiting: bool = False)

    max_query_count: int  # Maximum number of transfer request statuses to query.
    skip_waiting: bool    # Whether to skip requests with status TransferStatus.WAITING. True skips them; False includes them.
```

**Example**

```python
args = hixl.GetTransferStatusArgs(max_query_count=10, skip_waiting=True)
```

## TransferResult

Result information for each request when querying all asynchronous transfer request statuses.

```python
class TransferResult:
    req: int                # Transfer request handle (int).
    user_data: int          # User-defined information (int).
    status: TransferStatus  # Transfer request status (read-only).
```

## NotifyDesc

Notify description.

```python
class NotifyDesc:
    def __init__(self, name: str, notify_msg: str)

    name: str         # Notify name, up to 1024 bytes after UTF-8 encoding.
    notify_msg: str   # Notify message content, up to 1024 bytes after UTF-8 encoding.
```

**Example**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
```

## FeatureType

Library capability type queried through **get\_capability**. Enum values must be assigned explicitly. New capabilities may be appended only at the end.

```python
class FeatureType:
    AUTO_CONNECT = 0       # Auto Connect mode.
    CLIENT_SERVER_COMM = 1 # Client/Server communication mode.
```

| Enum Value | Description |
| --- | --- |
| AUTO_CONNECT | Auto Connect mode, corresponding to **OPTION\_AUTO\_CONNECT** in **Initialize**. |
| CLIENT_SERVER_COMM | Client/Server communication mode, where the Server listens on a port and the Client initiates a connection. |

## FEATURE\_SUPPORTED / FEATURE\_NOT\_SUPPORTED

Constants for the **value** output parameter of **get\_capability**.

```python
FEATURE_SUPPORTED = 1      # Capability supported.
FEATURE_NOT_SUPPORTED = 0  # Capability unsupported.
```
