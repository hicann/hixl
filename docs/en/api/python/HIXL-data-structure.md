# HIXL Data Structures

## MemDesc

Memory description information.

```python
class MemDesc:
    def __init__(self, addr: int, len: int)

    addr: int   # Memory address
    len: int    # Memory length (bytes)
```

**Example**

```python
mem_desc = hixl.MemDesc(addr=0x7f0000000000, len=1024*1024)
```

## MemHandle

Memory Handle.

In Python, this is an int type, returned by the register\_mem interface.

## MemType

Memory type.

```python
class MemType:
    MEM_DEVICE = 0  # Device memory
    MEM_HOST = 1    # Host memory
```

## AsyncConnectStatus

Asynchronous link establishment/disconnection status.

```python
class AsyncConnectStatus:
    NOT_CONNECT = 0         # Not connected
    CONNECT_PENDING = 1     # Link establishment pending
    CONNECTING = 2          # Link establishment in progress
    CONNECTED = 3           # Link establishment successful
    CONNECT_FAILED = 4      # Link establishment failed
    DISCONNECT_PENDING = 5  # Disconnection pending
    DISCONNECTING = 6       # Disconnection in progress
```

## TransferOp

Transfer operation type.

```python
class TransferOp:
    READ = 0   # Read from remote to local
    WRITE = 1  # Write from local to remote
```

## TransferOpDesc

Transfer operation description information.

```python
class TransferOpDesc:
    def __init__(self, local_addr: int, remote_addr: int, len: int)

    local_addr: int   # Local memory address
    remote_addr: int  # Remote memory address
    len: int          # Transfer length (bytes)
```

**Example**

```python
op_desc = hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=4096)
```

## TransferArgs

Optional parameters for transfer operations.

```python
class TransferArgs:
    def __init__(self)

    user_data: int  # User-defined information (int type), used with get all asynchronous transfer request status interface
```

**Example**

```python
args = hixl.TransferArgs()
args.user_data = 12345
```

## TransferReq

Transfer request Handle.

In Python, this is an int type, returned by the transfer\_async interface.

## TransferStatus

Asynchronous transfer status.

```python
class TransferStatus:
    WAITING = 0    # Waiting
    COMPLETED = 1  # Completed
    TIMEOUT = 2    # Timeout (not supported yet)
    FAILED = 3     # Failed
```

## GetTransferStatusArgs

Parameters for getting all asynchronous transfer request statuses.

```python
class GetTransferStatusArgs:
    def __init__(self, max_query_count: int = 0xFFFFFFFF, skip_waiting: bool = False)

    max_query_count: int  # Maximum number of transfer request statuses to query
    skip_waiting: bool    # Whether to skip transfer requests with status TransferStatus.WAITING, True means skip, False means do not skip
```

**Example**

```python
args = hixl.GetTransferStatusArgs(max_query_count=10, skip_waiting=True)
```

## TransferResult

Result information for each request when getting all asynchronous transfer request statuses.

```python
class TransferResult:
    req: int                # Transfer request handle (int type)
    user_data: int          # User-defined information (int type)
    status: TransferStatus  # Transfer request status (read-only)
```

## NotifyDesc

Notify description information.

```python
class NotifyDesc:
    def __init__(self, name: str, notify_msg: str)

    name: str         # Notify name, maximum length 1024 characters
    notify_msg: str   # Notify message content, maximum length 1024 characters
```

**Example**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
```

## FeatureType

Library capability feature type, used for get\_capability interface query. Enum values must be explicitly assigned, new capabilities are only allowed to be appended at the end.

```python
class FeatureType:
    AUTO_CONNECT = 0       # Auto Connect mode
    CLIENT_SERVER_COMM = 1 # Client/Server communication mode
```

| Enum Value | Description |
| --- | --- |
| AUTO_CONNECT | Auto Connect mode, corresponds to OPTION\_AUTO\_CONNECT option during Initialize. |
| CLIENT_SERVER_COMM | Client/Server communication mode, i.e., Server listens on port and Client initiates link establishment capability. |

## FEATURE\_SUPPORTED / FEATURE\_NOT\_SUPPORTED

Value constants for the value output parameter of the get\_capability interface.

```python
FEATURE_SUPPORTED = 1      # Feature supported
FEATURE_NOT_SUPPORTED = 0  # Feature not supported
```
